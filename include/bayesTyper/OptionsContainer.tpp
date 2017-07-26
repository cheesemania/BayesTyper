#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>

#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string.hpp"

#include "Utils.hpp"
#include "Regions.hpp"

OptionsContainer::OptionsContainer(const std::string & version_in, const std::string & start_time_in) : version(version_in), start_time(start_time_in) {}


OptionsContainer::~OptionsContainer() {

    for (auto &option: options) {

        delete option.second.first;
    }
}


template<typename ValueType>
void OptionsContainer::parseValue(std::string option, ValueType value) {

    OptionValue<ValueType> * option_value = new OptionValue<ValueType>(value);

    std::stringstream value_stream;
    value_stream << value;   

    assert(options.emplace(option, std::pair<OptionValueBase*, std::string>(option_value, value_stream.str())).second);
}


template<typename ValueType>
void OptionsContainer::parseValuePair(std::string option, std::string value_pair_str) {

    std::pair<ValueType,ValueType> value_pair;

    std::vector<std::string> value_pair_str_split;
    split(value_pair_str_split, value_pair_str, boost::is_any_of(","));

    assert(value_pair_str_split.size() == 2);

    if (typeid(ValueType) == typeid(uint)) {

        value_pair.first = stoi(value_pair_str_split.front());
        value_pair.second = stoi(value_pair_str_split.back());

    } else {

        value_pair.first = stod(value_pair_str_split.front());
        value_pair.second = stod(value_pair_str_split.back());
    }

    OptionValue<std::pair<ValueType,ValueType> > * option_value = new OptionValue<std::pair<ValueType,ValueType> >(value_pair);

    assert(options.emplace(option, std::pair<OptionValueBase*, std::string>(option_value, value_pair_str)).second);
}


void OptionsContainer::parseFiles(std::string option, std::string files) {

    std::vector<std::string> filenames;
    split(filenames, files, boost::is_any_of(","));

    OptionValue<std::vector<std::string> > * option_value = new OptionValue<std::vector<std::string> >(filenames);
    
    assert(option_value->value.size() >= 1);
    assert(options.emplace(option, std::pair<OptionValueBase*, std::string>(option_value, files)).second);
}


void OptionsContainer::parseRegions(std::string option, std::string regions) {

    OptionValue<Regions> * option_value = new OptionValue<Regions>(Regions(regions));
    assert(options.emplace(option, std::pair<OptionValueBase*, std::string>(option_value, regions)).second);
}


template<typename ValueType>
const ValueType & OptionsContainer::getValue(std::string option) const {

    return static_cast<OptionValue<ValueType> * >(options.at(option).first)->value;
}


template<typename ValueType>
void OptionsContainer::updateValue(std::string option, ValueType new_value) {

    auto options_it = options.find(option);
    assert(options_it != options.end());

    static_cast<OptionValue<ValueType> * >(options_it->second.first)->value = new_value;

    std::stringstream value_stream;
    value_stream << new_value;   

    options_it->second.second = value_stream.str();
}


std::string OptionsContainer::writeHeader() {

    std::stringstream header_stream;

    header_stream << "##CommandLine=<ID=BayesTyper,Version=\"" << version << "\",Time=\"" << start_time << "\",Options=\"";

    auto oit = options.cbegin();

    header_stream << oit->first << "=" << oit->second.second;
    oit++;

    while (oit != options.cend()) {

        header_stream << ", " << oit->first << "=" << oit->second.second;
        oit++;
    }

    header_stream << "\">\n";

    return header_stream.str();
}


