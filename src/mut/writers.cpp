#include <cassert>
#include <iostream>
#include <fstream>

#include <morphio/mut/writers.h>
#include <morphio/mut/mitochondria.h>
#include <morphio/mut/morphology.h>
#include <morphio/mut/section.h>

#include "../plugin/errorMessages.h"

#include <highfive/H5File.hpp>
#include <highfive/H5DataSet.hpp>
#include <highfive/H5Object.hpp>

namespace morphio
{
namespace mut
{
namespace writer
{
template <typename T>
struct base_type
{
    using type = T;
};

/**
   A structure to get the base type of nested vectors
 **/
template <typename T>
struct base_type<std::vector<T>> : base_type<T>
{
};

void swc(const Morphology& morphology, const std::string& filename)
{
    std::ofstream myfile;
    myfile.open (filename);
    using std::setw;

    myfile << "# index" << setw(9)
           << "type" << setw(9)
           << "X" << setw(12)
           << "Y" << setw(12)
           << "Z" << setw(12)
           << "radius" << setw(13)
           << "parent\n" << std::endl;


    int segmentIdOnDisk = 1;
    std::map<uint32_t, int32_t> newIds;
    auto soma = morphology.soma();

    if(morphology.soma()->points().size() < 1)
        throw WriterError(plugin::ErrorMessages().ERROR_WRITE_NO_SOMA());

    for (int i = 0; i < soma->points().size(); ++i){
        myfile << segmentIdOnDisk++ << setw(12) << SECTION_SOMA << setw(12)
               << soma->points()[i][0] << setw(12) << soma->points()[i][1] << setw(12)
               << soma->points()[i][2] << setw(12) << soma->diameters()[i] / 2. << setw(12)
                  << (i==0 ? -1 : segmentIdOnDisk-1) << std::endl;
    }

    for(auto it = morphology.depth_begin(); it != morphology.depth_end(); ++it) {
        int32_t sectionId = *it;
        auto section = morphology.section(sectionId);
        const auto& points = section->points();
        const auto& diameters = section->diameters();

        assert(points.size() > 0 && "Empty section");
        bool isRootSection = morphology.parent(sectionId) < 0;
        for (int i = (isRootSection ? 0 : 1); i < points.size(); ++i)
        {
            myfile << segmentIdOnDisk << setw(12) << section->type() << setw(12)
                   << points[i][0] << setw(12) << points[i][1] << setw(12)
                   << points[i][2] << setw(12) << diameters[i] / 2. << setw(12);

            if (i > (isRootSection ? 0 : 1))
                myfile << segmentIdOnDisk - 1 << std::endl;
            else {
                int32_t parentId = morphology.parent(sectionId);
                myfile << (parentId != -1 ? newIds[parentId] : 1) << std::endl;
            }

            ++segmentIdOnDisk;
        }
        newIds[section->id()] = segmentIdOnDisk - 1;
    }

    myfile.close();

}

void _write_asc_points(std::ofstream& myfile, const Points& points,
                       const std::vector<float>& diameters, int indentLevel)
{
    for (int i = 0; i < points.size(); ++i)
    {
        myfile << std::string(indentLevel, ' ') << "(" << points[i][0] << ' '
                  << points[i][1] << ' ' << points[i][2] << ' ' << diameters[i]
                  << ')' << std::endl;
    }
}

void _write_asc_section(std::ofstream& myfile, const Morphology& morpho, uint32_t id, int indentLevel)
{
    std::string indent(indentLevel, ' ');
    auto section = morpho.section(id);
    _write_asc_points(myfile, section->points(), section->diameters(), indentLevel);

    if (!morpho.children(id).empty())
    {
        auto children = morpho.children(id);
        size_t nChildren = children.size();
        for (int i = 0; i<nChildren; ++i)
        {
            myfile << indent << (i == 0 ? "(" : "|") << std::endl;
            _write_asc_section(myfile, morpho, children[i], indentLevel + 2);
        }
        myfile << indent << ")" << std::endl;
    }
}

void asc(const Morphology& morphology, const std::string& filename)
{
    std::ofstream myfile;
    myfile.open (filename);

    std::map<morphio::SectionType, std::string> header;
    header[SECTION_AXON] = "( (Color Cyan)\n  (Axon)\n";
    header[SECTION_DENDRITE] = "( (Color Red)\n  (Dendrite)\n";
    header[SECTION_APICAL_DENDRITE] = "( (Color Red)\n  (Apical)\n";

    const auto soma = morphology.soma();
    if(soma->points().size() > 0) {
        myfile << "(\"CellBody\"\n  (Color Red)\n  (CellBody)\n";
        _write_asc_points(myfile, soma->points(), soma->diameters(), 2);
        myfile << ")\n\n";
    } else {
        LBERROR(plugin::ErrorMessages().ERROR_WRITE_NO_SOMA());
    }


    for (auto& id : morphology.rootSections())
    {
        myfile << header.at(morphology.section(id)->type());
        _write_asc_section(myfile, morphology, id, 2);
        myfile << ")\n\n";
    }

    myfile.close();

}

template <typename T>
HighFive::Attribute write_attribute(HighFive::File& file,
                                    const std::string& name, const T& version)
{
    HighFive::Attribute a_version =
        file.createAttribute<typename T::value_type>(name,
                                                     HighFive::DataSpace::From(
                                                         version));
    a_version.write(version);
    return a_version;
}

template <typename T>
HighFive::Attribute write_attribute(HighFive::Group& group,
                                    const std::string& name, const T& version)
{
    HighFive::Attribute a_version =
        group.createAttribute<typename T::value_type>(name,
                                                      HighFive::DataSpace::From(
                                                          version));
    a_version.write(version);
    return a_version;
}

template <typename T>
void write_dataset(HighFive::File& file, const std::string& name, const T& raw)
{
    HighFive::DataSet dpoints = file.createDataSet<typename base_type<T>::type>(
        name, HighFive::DataSpace::From(raw));

    dpoints.write(raw);
}

template <typename T>
void write_dataset(HighFive::Group& file, const std::string& name, const T& raw)
{
    HighFive::DataSet dpoints = file.createDataSet<typename base_type<T>::type>(
        name, HighFive::DataSpace::From(raw));

    dpoints.write(raw);
}

void mitochondriaH5(HighFive::File& h5_file, const Mitochondria& mitochondria,
                    const std::string& filename)
{
    if(mitochondria.rootSections().empty())
        return;

    Property::Properties properties;
    mitochondria._buildMitochondria(properties);
    auto& p = properties._mitochondriaPointLevel;
    size_t size = p._diameters.size();

    std::vector<std::vector<float>> points;
    std::vector<std::vector<int32_t>> structure;
    for (int i = 0; i < size; ++i)
    {
        points.push_back(
            {(float)p._sectionIds[i], p._relativePathLengths[i], p._diameters[i]});
    }

    auto& s = properties._mitochondriaSectionLevel;
    for (int i = 0; i < s._sections.size(); ++i)
    {
        structure.push_back({s._sections[i][0], s._sections[i][1]});
    }

    HighFive::Group g_mitochondria = h5_file.createGroup("mitochondria");

    write_dataset(g_mitochondria, "/mitochondria/points", points);
    write_dataset(h5_file, "/mitochondria/structure", structure);
}

void h5(const Morphology& morpho, const std::string& filename)
{
    HighFive::File h5_file(filename, HighFive::File::ReadWrite |
                                         HighFive::File::Create |
                                         HighFive::File::Truncate);

    int sectionIdOnDisk = 1;
    int i = 0;
    std::map<uint32_t, int32_t> newIds;

    std::vector<std::vector<float>> raw_points;
    std::vector<std::vector<int32_t>> raw_structure;
    std::vector<float> raw_perimeters;

    const auto &points = morpho.soma()->points();
    const auto &diameters = morpho.soma()->diameters();

    const std::size_t numberOfPoints = points.size();
    const std::size_t numberOfDiameters = diameters.size();


    if(numberOfPoints < 1)
        throw WriterError(plugin::ErrorMessages().ERROR_WRITE_NO_SOMA());
    if(numberOfPoints != numberOfDiameters)
        throw WriterError(plugin::ErrorMessages().ERROR_VECTOR_LENGTH_MISMATCH(
                              "soma points", numberOfPoints,
                              "soma diameters", numberOfDiameters));

    bool hasPerimeterData = morpho.rootSections().size() > 0 ?
        morpho.section(morpho.rootSections()[0])->perimeters().size() > 0 :
        false;

    for(int i = 0;i<numberOfPoints; ++i) {
        raw_points.push_back({points[i][0], points[i][1], points[i][2], diameters[i]});
        if(hasPerimeterData)
            raw_perimeters.push_back(0);
    }

    raw_structure.push_back({0, SECTION_SOMA, -1});
    int offset = 0;
    offset += morpho.soma()->points().size();




    for(auto it = morpho.depth_begin(); it != morpho.depth_end(); ++it) {
        uint32_t sectionId = *it;
        uint32_t parentId = morpho.parent(sectionId);
        int parentOnDisk = (parentId != -1 ? newIds[parentId] : 0);

        auto section = morpho.section(sectionId);
        const auto &points = section->points();
        const auto &diameters = section->diameters();
        const auto &perimeters = section->perimeters();

        const std::size_t numberOfPoints = points.size();
        const std::size_t numberOfPerimeters = perimeters.size();
        raw_structure.push_back({offset, section->type(), parentOnDisk});

        for(int i = 0;i<numberOfPoints; ++i)
            raw_points.push_back({points[i][0], points[i][1], points[i][2], diameters[i]});

        if(numberOfPerimeters > 0) {

            if(numberOfPerimeters != numberOfPoints)
                throw WriterError(plugin::ErrorMessages().ERROR_VECTOR_LENGTH_MISMATCH(
                                      "points", numberOfPoints,
                                      "perimeters", numberOfPerimeters));
            for(int i = 0;i<numberOfPerimeters; ++i)
                raw_perimeters.push_back(perimeters[i]);
        }

        newIds[section->id()] = sectionIdOnDisk++;
        offset += numberOfPoints;
    }

    write_dataset(h5_file, "/points", raw_points);
    write_dataset(h5_file, "/structure", raw_structure);

    HighFive::Group g_metadata = h5_file.createGroup("metadata");

    write_attribute(g_metadata, "version", std::vector<uint32_t>{1, 1});
    write_attribute(g_metadata, "cell_family",
                    std::vector<uint32_t>{FAMILY_NEURON});
    write_attribute(h5_file, "comment",
                    std::vector<std::string>{" created out by morpho_tool v1"});

    if (hasPerimeterData)
        write_dataset(h5_file, "/perimeters", raw_perimeters);

    mitochondriaH5(h5_file, morpho.mitochondria(), filename);
}

} // end namespace writer
} // end namespace mut
} // end namespace morphio