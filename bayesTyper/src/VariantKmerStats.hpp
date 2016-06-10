
/*
VariantKmerStats.hpp - This file is part of BayesTyper (v0.9)


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


#ifndef __bayesTyper__VariantKmerStats_hpp
#define __bayesTyper__VariantKmerStats_hpp

#include <vector>

#include "Utils.hpp"
#include "KmerCounts.hpp"
#include "KmerCoverage.hpp"

using namespace std;

class VariantKmerStats {

	public:
		
		vector<vector<float> > mean_num_kmers;
		vector<vector<float> > mean_num_observed_kmers;
		vector<vector<float> > mean_num_unique_kmers;

		VariantKmerStats();
		VariantKmerStats(const ushort, const ushort);

		void addKmerCoverage(const ushort, const KmerCoverage &, const vector<double> &);

	private:

		vector<vector<float> > sum_weights_num_kmers;
		vector<vector<float> > sum_weights_num_observed_kmers;
		vector<vector<float> > sum_weights_num_unique_kmers;

		float updateMean(const float, const float, const float, const float);

};

#endif