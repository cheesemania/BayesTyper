
/*
InferenceEngine.cpp - This file is part of BayesTyper (v0.9)


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


#include <vector>
#include <thread>
#include <mutex>
#include <random>

#include "ProducerConsumerQueue.hpp"

#include "InferenceEngine.hpp"
#include "Utils.hpp"
#include "VariantClusterGroup.hpp" 
#include "CountDistribution.hpp"
#include "Genotypes.hpp"
#include "GenotypeWriter.hpp"
#include "KmerHash.hpp"
#include "ChromosomePloidy.hpp"
#include "Sample.hpp"
#include "OptionsContainer.hpp"
#include "Regions.hpp"


static const uint parameter_estimation_stdout_frequency = 10;
static const uint genotyping_stdout_frequency = 10000;
static const uint variant_cluster_groups_batch_size = 100;


InferenceEngine::InferenceEngine(const vector<Sample> & samples, const uchar num_noise_sources_in, const OptionsContainer & options_container) : num_samples(samples.size()), num_noise_sources(num_noise_sources_in), chromosome_ploidy(samples), chromosome_regions(options_container.getValue<Regions>("chromosome-regions")), prng_seed(options_container.getValue<uint>("random-seed")), num_threads(options_container.getValue<ushort>("threads")), num_haplotype_candidates_per_sample(options_container.getValue<ushort>("number-of-haplotype-candidates-per-sample")), gibbs_burn(options_container.getValue<ushort>("gibbs-burn-in")), gibbs_samples(options_container.getValue<ushort>("gibbs-samples")), num_gibbs_sampling_chains(options_container.getValue<ushort>("number-of-gibbs-chains")), num_parameter_estimation_samples(options_container.getValue<ushort>("number-of-parameter-estimation-samples")), num_parameter_estimation_variants(options_container.getValue<uint>("number-of-parameter-estimation-variants")), max_multicluster_kmers(options_container.getValue<uint>("maximum-number-of-multicluster-kmers")) {}


void InferenceEngine::allocateShuffledIndicesToThreads(vector<vector<uint> > * thread_index_allocation, const uint num_indices) {

    mt19937 prng = mt19937(prng_seed);

    assert(thread_index_allocation->empty());

    uint num_indices_per_thread = num_indices/num_threads + 1;
    uint num_last_indices = num_indices%num_threads;

    for (uint i = 0; i < num_threads; i++) {

        thread_index_allocation->push_back(vector<uint>(num_indices_per_thread));

        for (uint j = 0; j < thread_index_allocation->back().size(); j++) {

            thread_index_allocation->back().at(j) = i + j * num_threads; 
        }

        if (i >= num_last_indices) {

            thread_index_allocation->back().pop_back();
        }

        shuffle(thread_index_allocation->back().begin(), thread_index_allocation->back().end(), prng);
    }
}


void InferenceEngine::selectVariantsClusterGroupsForParameterEstimationCallback(vector<VariantClusterGroup*> * variant_cluster_groups, const vector<uint> & variant_cluster_group_indices, vector<VariantClusterGroup*> * selected_variant_cluster_groups, KmerHash * kmer_hash, const vector<Sample> & samples, const uint num_parameter_estimation) {

	assert(!(variant_cluster_group_indices.empty()));

	for (auto &variant_cluster_groups_idx: variant_cluster_group_indices) {

		VariantClusterGroup * variant_cluster_group = variant_cluster_groups->at(variant_cluster_groups_idx);

		if (!(variant_cluster_group->isAutosomal()) or !(variant_cluster_group->isSingleNucleotidePolymorphism()) or variant_cluster_group->hasAmbiguousNucleotide() or variant_cluster_group->hasNonUniqueKmer()) {
			
			continue;
		}

		assert(!(variant_cluster_group->hasComplexRegion()));
		assert(!(variant_cluster_group->hasRedundantSequence()));
		
		assert(variant_cluster_group->numberOfVariants() == 1);
		assert(variant_cluster_group->numberOfVariantClusters() == 1);
		assert(variant_cluster_group->numberOfVariantClusterGroupTrees() == 1);

		variant_cluster_group->initialise(kmer_hash, samples, prng_seed, num_noise_sources, num_haplotype_candidates_per_sample, max_multicluster_kmers);

		assert(!(variant_cluster_group->hasInterclusterKmer()));
		assert(!(variant_cluster_group->hasMulticlusterKmer()));

		if (!(variant_cluster_group->hasExcludedKmer())) {

			selected_variant_cluster_groups->push_back(variant_cluster_group);
			
			if (selected_variant_cluster_groups->size() == num_parameter_estimation) {

				break;
			}
		}
	}
}


void InferenceEngine::allocateCountsForParameterEstimationCallback(vector<VariantClusterGroup*> * variant_cluster_groups, const CountDistribution & count_distribution, CountAllocation * count_allocation_global, mutex * count_allocation_lock) {

	CountAllocation count_allocation_local(num_noise_sources, num_samples);

	auto lit = variant_cluster_groups->begin();

	while (lit != variant_cluster_groups->end()) {

		assert((*lit)->numberOfVariants() == 1);
		assert((*lit)->numberOfVariantClusters() == 1);
		assert((*lit)->numberOfVariantClusterGroupTrees() == 1);
		
		(*lit)->estimateGenotypes(count_distribution, chromosome_ploidy, false);
		(*lit)->sampleCountAllocations(count_distribution, &count_allocation_local);		

		lit++;
	} 

	lock_guard<mutex> count_locker(*count_allocation_lock);
	count_allocation_global->mergeInCountAllocations(count_allocation_local);
}


vector<vector<double> > InferenceEngine::meanParameterEstimationSamples(const vector<vector<vector<double> > > & parameter_estimation_samples) {

	assert(parameter_estimation_samples.size() == (gibbs_burn + num_parameter_estimation_samples));

	vector<vector<double> > mean_parameter_estimation_samples(num_samples, vector<double>(num_noise_sources, 0));

	for (uint sample_idx = 0; sample_idx < num_samples; sample_idx++) {

		for (uint noise_idx = 0; noise_idx < num_noise_sources; noise_idx++) {

			for (uint i = gibbs_burn; i < (gibbs_burn + num_parameter_estimation_samples); i++) {

				assert(parameter_estimation_samples.at(i).size() == num_samples);
				assert(parameter_estimation_samples.at(i).at(sample_idx).size() == num_noise_sources);

				mean_parameter_estimation_samples.at(sample_idx).at(noise_idx) += parameter_estimation_samples.at(i).at(sample_idx).at(noise_idx);
			}

			mean_parameter_estimation_samples.at(sample_idx).at(noise_idx) /= num_parameter_estimation_samples;
		}
	}

	return mean_parameter_estimation_samples;
}


void InferenceEngine::estimateNoiseParameters(CountDistribution * count_distribution, vector<VariantClusterGroup*> * variant_cluster_groups, KmerHash * kmer_hash, const vector<Sample> & samples) {
	
	vector<vector<uint> > thread_variant_cluster_group_index_allocation;
	allocateShuffledIndicesToThreads(&thread_variant_cluster_group_index_allocation, variant_cluster_groups->size());
	assert(thread_variant_cluster_group_index_allocation.size() == num_threads);

	// Calculate fraction of variant_cluster_groups to use for count parameter estimation
	uint thread_num_parameter_estimation = ceil(num_parameter_estimation_variants/static_cast<double>(num_threads));

	cout << "\n[" << Utils::getLocalTime() << "] Selecting autosomal single nucleotide polymorphism clusters for noise parameter estimation ..." << endl;

	vector<thread> parameter_estimation_selection_threads;
	parameter_estimation_selection_threads.reserve(num_threads);

	vector<vector<VariantClusterGroup*> * > thread_selected_variant_cluster_groups;
	thread_selected_variant_cluster_groups.reserve(num_threads);

	for (uint i = 0; i < num_threads; i++) {

		if (!(thread_variant_cluster_group_index_allocation.at(i).empty())) {

			thread_selected_variant_cluster_groups.push_back(new vector<VariantClusterGroup*>());
			thread_selected_variant_cluster_groups.back()->reserve(thread_num_parameter_estimation);
	   	    
	   	    parameter_estimation_selection_threads.push_back(thread(&InferenceEngine::selectVariantsClusterGroupsForParameterEstimationCallback, this, variant_cluster_groups, ref(thread_variant_cluster_group_index_allocation.at(i)), thread_selected_variant_cluster_groups.back(), kmer_hash, ref(samples), thread_num_parameter_estimation));
   		}
    }

	thread_selected_variant_cluster_groups.shrink_to_fit();

    for (auto & thread : parameter_estimation_selection_threads) {
        	
       	thread.join();
	}

	uint num_selected_parameter_estimation_variants = 0;

	for (uint i = 0; i < thread_selected_variant_cluster_groups.size(); i++) {

		thread_selected_variant_cluster_groups.at(i)->shrink_to_fit();
		assert(thread_selected_variant_cluster_groups.at(i)->size() <= thread_num_parameter_estimation);

		if ((num_parameter_estimation_variants % num_threads) > 0) {

			if ((thread_selected_variant_cluster_groups.at(i)->size() == thread_num_parameter_estimation) and (i >= (num_parameter_estimation_variants % num_threads))) {

				thread_selected_variant_cluster_groups.at(i)->pop_back();
			}
		}

		num_selected_parameter_estimation_variants += thread_selected_variant_cluster_groups.at(i)->size();
	}

	cout << "[" << Utils::getLocalTime() << "] Running a " << gibbs_burn << " burn-in iterations and " << num_parameter_estimation_samples << " parameter estimation iterations on " << num_selected_parameter_estimation_variants << " randomly selected single nucleotide polymorphism clusters ...\n" << endl;

	mutex count_allocation_lock;

	for (uint i = 0; i < (gibbs_burn + num_parameter_estimation_samples); i++) {
	
		CountAllocation count_allocation(num_noise_sources, num_samples);

	    vector<thread> count_allocation_threads;
	    count_allocation_threads.reserve(thread_selected_variant_cluster_groups.size());

		for (uint j = 0; j < thread_selected_variant_cluster_groups.size(); j++) {

    	    count_allocation_threads.push_back(thread(&InferenceEngine::allocateCountsForParameterEstimationCallback, this, thread_selected_variant_cluster_groups.at(j), ref(*count_distribution), &count_allocation, &count_allocation_lock));
	    }

    	for(auto & thread : count_allocation_threads) {
        	
        	thread.join();
		}	    

		count_distribution->sampleParameters(count_allocation);

		if (((i+1)%parameter_estimation_stdout_frequency) == 0) {

			if ((i+1) < (gibbs_burn)) {

				cout << "[" << Utils::getLocalTime() << "] Gibbs sampler: Completed " << i+1 << " burn-in iterations" << endl;
			}

			if (((i+1) > gibbs_burn) and ((i+1) < (gibbs_burn + num_parameter_estimation_samples))) {

				cout << "[" << Utils::getLocalTime() << "] Gibbs sampler: Completed " << i+1 - gibbs_burn << " parameter estimation iterations" << endl;
			}
		}	
		
		if ((i+1) == (gibbs_burn)) {

			cout << "[" << Utils::getLocalTime() << "] Gibbs sampler: Completed " << i+1 << " burn-in iterations\n" << endl;
		}
	}

	for (uint i = 0; i < thread_selected_variant_cluster_groups.size(); i++) {

		delete thread_selected_variant_cluster_groups.at(i);
	}

	cout << "[" << Utils::getLocalTime() << "] Gibbs sampler: Completed " << num_parameter_estimation_samples << " parameter estimation iterations" << endl;
	
	count_distribution->setNoiseZeroInflations(meanParameterEstimationSamples(count_distribution->noiseZeroInflationsSamples())); 
	count_distribution->setNoiseRates(meanParameterEstimationSamples(count_distribution->noiseRatesSamples()));

	cout << "\n[" << Utils::getLocalTime() << "] Fixed noise model parameters to the mean of the " << num_parameter_estimation_samples << " parameter estimates"<< endl;
}


void InferenceEngine::genotypeVariantClusterGroupsCallback(ProducerConsumerQueue<VariantClusterGroupBatch> * variant_cluster_group_batch_queue, KmerHash * kmer_hash, const CountDistribution & count_distribution, const vector<Sample> & samples, GenotypeWriter * genotype_writer, VariantClusterGroupCounts * variant_cluster_group_counts, mutex * thread_lock) {

	assert(samples.size() == num_samples);

    mt19937 prng = mt19937(prng_seed);

	VariantClusterGroupCounts variant_cluster_group_counts_local;
	VariantClusterGroupBatch variant_cluster_group_batch;

	while (variant_cluster_group_batch_queue->pop(&variant_cluster_group_batch)) {

		vector<Genotypes*> * variant_genotypes = new vector<Genotypes*>();
		variant_genotypes->reserve(variant_cluster_group_batch.number_of_variants);

		auto variant_cluster_group_it = variant_cluster_group_batch.start_it;

		while (variant_cluster_group_it != variant_cluster_group_batch.end_it) {

			if ((*variant_cluster_group_it)->isInChromosomeRegions(chromosome_regions)) {

				for (ushort chain_idx = 0; chain_idx < num_gibbs_sampling_chains; chain_idx++) {

					(*variant_cluster_group_it)->initialise(kmer_hash, samples, prng_seed, num_noise_sources, num_haplotype_candidates_per_sample, max_multicluster_kmers);
					(*variant_cluster_group_it)->shuffleBranches(&prng);

					for (ushort i = 0; i < gibbs_burn; i++) {

						(*variant_cluster_group_it)->estimateGenotypes(count_distribution, chromosome_ploidy, false);
					}

					for (ushort i = 0; i < gibbs_samples; i++) {

						(*variant_cluster_group_it)->estimateGenotypes(count_distribution, chromosome_ploidy, true);
					}
				}

				if ((*variant_cluster_group_it)->hasMulticlusterKmer()) {

					assert((*variant_cluster_group_it)->numberOfVariantClusters() > 1);
					variant_cluster_group_counts_local.num_multicluster++;

				} else if ((*variant_cluster_group_it)->hasInterclusterKmer()) {

					variant_cluster_group_counts_local.num_intercluster++;
				
				} else {

					variant_cluster_group_counts_local.num_unique++;
				}

				(*variant_cluster_group_it)->collectGenotypes(variant_genotypes, chromosome_ploidy);

			} else {

				variant_cluster_group_counts_local.num_skipped++;
			}
	
			delete *variant_cluster_group_it;			
			variant_cluster_group_it++;
		}

		genotype_writer->addGenotypes(variant_genotypes);
	}

	lock_guard<mutex> tread_locker(*thread_lock);
	*variant_cluster_group_counts += variant_cluster_group_counts_local;
}


void InferenceEngine::genotypeVariantClusterGroups(vector<VariantClusterGroup*> * variant_cluster_groups, KmerHash * kmer_hash, const CountDistribution & count_distribution, const vector<Sample> & samples, GenotypeWriter * genotype_writer) {

	cout << "\n[" << Utils::getLocalTime() << "] Running " << num_gibbs_sampling_chains << " parallel gibbs sampling chains with " << gibbs_burn + gibbs_samples << " iterations (" << gibbs_burn << " burn-in) on " << variant_cluster_groups->size() << " variant cluster groups ...\n" << endl;

    ProducerConsumerQueue<VariantClusterGroupBatch> variant_cluster_group_batch_queue(Utils::queue_size_scaling * num_threads);

    VariantClusterGroupCounts variant_cluster_group_counts;
    mutex thread_lock;

	vector<thread> genotype_threads;
	genotype_threads.reserve(num_threads);

	for (int i=0; i < num_threads; i++) {

        genotype_threads.push_back(thread(&InferenceEngine::genotypeVariantClusterGroupsCallback, this, &variant_cluster_group_batch_queue, kmer_hash, ref(count_distribution), ref(samples), genotype_writer, &variant_cluster_group_counts, &thread_lock));
    }  

    auto variant_cluster_group_it = variant_cluster_groups->begin();
    auto first_variant_cluster_group_it = variant_cluster_group_it;

	uint variant_cluster_group_counter = 0;
	uint number_of_batch_variants = 0;
	
	while (variant_cluster_group_it != variant_cluster_groups->end()) {

		assert(*variant_cluster_group_it);
		number_of_batch_variants += (*variant_cluster_group_it)->numberOfVariants();

		variant_cluster_group_counter++;
		variant_cluster_group_it++;

		if (number_of_batch_variants >= variant_cluster_groups_batch_size) {

			variant_cluster_group_batch_queue.push(VariantClusterGroupBatch(number_of_batch_variants, first_variant_cluster_group_it, variant_cluster_group_it));	
			
			first_variant_cluster_group_it = variant_cluster_group_it;
			number_of_batch_variants = 0;
		} 

		if ((variant_cluster_group_counter%genotyping_stdout_frequency) == 0) {

			if (variant_cluster_group_it != variant_cluster_groups->end()) {
			
				cout << "[" << Utils::getLocalTime() << "] Gibbs sampler: Processed " << variant_cluster_group_counter << " variant cluster groups" << endl;
			}	
		}
 	}

 	variant_cluster_group_batch_queue.push(VariantClusterGroupBatch(number_of_batch_variants, first_variant_cluster_group_it, variant_cluster_group_it));
	variant_cluster_group_batch_queue.pushedLast();

	cout << "[" << Utils::getLocalTime() << "] Gibbs sampler: Processed " << variant_cluster_group_counter << " variant cluster groups" << endl;	

	for(auto & thread : genotype_threads) {
    	
    	thread.join();
	}

	cout << "\n[" << Utils::getLocalTime() << "] Out of the " << variant_cluster_group_counter << " variant cluster groups:\n" << endl; 
    cout << "\t- " << variant_cluster_group_counts.num_multicluster << " had multicluster kmer(s)" << endl;
    cout << "\t- " << variant_cluster_group_counts.num_intercluster << " had intercluster kmer(s)" << endl;
    cout << "\t- " << variant_cluster_group_counts.num_unique << " had exclusively unique kmer(s)" << endl;
    cout << "\t- " << variant_cluster_group_counts.num_skipped << " were skipped (outside specified region)\n" << endl;
}


