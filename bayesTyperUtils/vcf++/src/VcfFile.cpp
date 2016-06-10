
/*
VcfFile.cpp - This file is part of BayesTyper (v0.9)


The MIT License (MIT)

Copyright (c) 2016 Jonas Andreas Sibbesen and Lasse Maretty

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#include <algorithm>
#include "assert.h"

#include "Utils.hpp"
#include "VcfFile.hpp"


VcfFile::VcfFile(const bool is_sorted_in) : is_sorted(is_sorted_in) {

  current_chromosome = "";
  current_position = -1;
}

void VcfFile::checkChromosomeAndPositionOrder(const string & cur_var_chrom, const uint cur_var_pos) {

  if (is_sorted) {

    assert(meta_data.getContig(cur_var_chrom).length() >= cur_var_pos);

    if (cur_var_chrom == current_chromosome) {

      assert(cur_var_pos >= current_position);
    
    } else if (!(current_chromosome.empty())) {

      assert(meta_data.getContigIndex(current_chromosome) < meta_data.getContigIndex(cur_var_chrom));
    }

    current_chromosome = cur_var_chrom;
    current_position = cur_var_pos;
  }
}

VcfMetaData & VcfFile::metaData() {

    return meta_data;
}


VcfFileReaderBase::VcfFileReaderBase(string vcf_filename, const bool is_sorted_in) : VcfFile(is_sorted_in) {

      vcf_file.open(vcf_filename);
      assert(vcf_file.is_open());

      last_line_read = false;
}



VcfFileReaderBase::~VcfFileReaderBase() {

      vcf_file.close();
}

VcfFileReader::VcfFileReader(string vcf_filename, const bool is_sorted_in) : VcfFileReaderBase(vcf_filename, is_sorted_in) {

  // Forward file to first variant line
  string cur_meta_line;

  while (std::getline(vcf_file, cur_meta_line)) {

	  if (cur_meta_line.front() == '#') {

      if (meta_data.addLine(cur_meta_line)) {

        break;
      }
    }
  }

  num_cols = meta_data.numColumns();
  assert(num_cols >= 8);

  cur_var_line = vector<string>(8);
  updateVariantLine();
}

void VcfFileReader::updateVariantLine() {

  for (uchar i = 0; i < 7; i++) {

    if (!getline(vcf_file, cur_var_line.at(i), '\t')) {

      last_line_read = true;
    }
  }

  if (!last_line_read) {

    if (num_cols == 8) {

      assert(getline(vcf_file, cur_var_line.at(7), '\n'));

    } else {

      assert(getline(vcf_file, cur_var_line.at(7), '\t'));
      vcf_file.ignore(numeric_limits<streamsize>::max(), '\n');
    }
  }

  assert(cur_var_line.at(7).find('\t') == string::npos);
}

bool VcfFileReader::getNextVariant(Variant ** variant) {

  if (last_line_read) {

    return false;
  }

  // Parse previous line
  *variant = new Variant(cur_var_line, meta_data);
  checkChromosomeAndPositionOrder((*variant)->chrom(), (*variant)->pos());

  // Read next line
  updateVariantLine();

  return true;
}

GenotypedVcfFileReader::GenotypedVcfFileReader(string vcf_filename, const bool is_sorted_in) : VcfFileReaderBase(vcf_filename, is_sorted_in) {

    // Forward file to first variant line
    string cur_meta_line;

    while (std::getline(vcf_file, cur_meta_line)) {

      if (meta_data.addLine(cur_meta_line)) {

        break;
      }
    }

  num_cols = meta_data.numColumns();
  assert(num_cols >= 8);

  cur_var_line = vector<string>(num_cols);
  updateVariantLine();
}

void GenotypedVcfFileReader::updateVariantLine() {

  for (uint i = 0; i < num_cols - 1; i++) {

    if (!getline(vcf_file, cur_var_line.at(i), '\t')) {

        last_line_read = true;
    }
  }

  if (!last_line_read) {

    assert(getline(vcf_file, cur_var_line.at(num_cols - 1), '\n'));
  }

  assert(cur_var_line.at(num_cols - 1).find('\t') == string::npos);
}

bool GenotypedVcfFileReader::getNextVariant(Variant ** variant) {

  if (last_line_read) {

    return false;
  }

  // Parse previous line
  *variant = new Variant(cur_var_line, meta_data);
  checkChromosomeAndPositionOrder((*variant)->chrom(), (*variant)->pos());

  // Read next line
  updateVariantLine();

  return true;
}

VcfFileWriter::VcfFileWriter(string vcf_filename, const VcfMetaData & meta_data_in, const bool is_sorted_in) : VcfFile(is_sorted_in) {

  vcf_file.open(vcf_filename);
  assert(vcf_file.is_open());

  meta_data = meta_data_in;

  vcf_file << meta_data.vcf() << "\n";
}

VcfFileWriter::~VcfFileWriter() {

  vcf_file.close();
}

const VcfMetaData & VcfFileWriter::metaData() const {

    return meta_data;
}

void VcfFileWriter::write(Variant * variant) {

  vcf_file << variant->vcf(meta_data) << "\n";

  checkChromosomeAndPositionOrder(variant->chrom(), variant->pos());
}
