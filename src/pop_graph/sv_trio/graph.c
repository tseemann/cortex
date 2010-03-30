#include <element.h>
#include <stdio.h>
#include <stdlib.h>
#include <file_reader.h>
#include <dB_graph.h>
#include <dB_graph_population.h>
#include <string.h>
#include <internal_oxford.h>



//#include <internal_tgac.h>

int main(int argc, char **argv){

  char* filename;
  int hash_key_bits, bucket_size;
  dBGraph * db_graph = NULL; 
  short kmer_size;
  int action;
  char* dumped_binary;
  char* list_of_fastq;
  boolean remove_low_covg_nodes;
  char* detectvars_filename;
  char* detectvars_after_remv_ref_bubble_filename;
  char* detectvars_hom_nonref_filename;
  char* ref_assisted_filename;
  char* ref_fasta;
  int low_cov_thresh;

  //command line arguments 
  filename         = argv[1];        //open file that lists one file per individual in the trio (population), and each of those gives list of files.
  kmer_size        = atoi(argv[2]);  //global variable defined in element.h
  hash_key_bits    = atoi(argv[3]);  //number of buckets: 2^hash_key_bits
  bucket_size      = atoi(argv[4]);
  action           = atoi(argv[5]);
  DEBUG            = atoi(argv[6]);
  dumped_binary   = argv[7];
  list_of_fastq = argv[8];
  remove_low_covg_nodes = (boolean) atoi(argv[9]);
  detectvars_filename = argv[10];
  detectvars_after_remv_ref_bubble_filename = argv[11];
  detectvars_hom_nonref_filename=argv[12];
  ref_assisted_filename=argv[13];
  ref_fasta = argv[14];
  low_cov_thresh = atoi(argv[15]);
  int max_retries=10;

  fprintf(stdout,"Kmer size: %d hash_table_size (%d bits): %d\n",kmer_size,hash_key_bits,1 << hash_key_bits);


  //Create the de Bruijn graph/hash table
  db_graph = hash_table_new(hash_key_bits,bucket_size, max_retries, kmer_size);
  printf("table created: %d\n",1 << hash_key_bits);

 
  printf("Start loading population\n");
  load_population_as_binaries_from_graph(filename, db_graph);
  printf("Finished loading population\n");
  printf("Total kmers in table: %qd\n", hash_table_get_unique_kmers(db_graph));







  switch (action)
    {
    case 0:
      {
	printf("Dump binary %s\n", dumped_binary);
	db_graph_dump_binary(dumped_binary,&db_node_check_status_not_pruned,db_graph);

	break;
      } 
    case 1:
      {
	printf("remove low coverage nodes (<= %d ) \n", low_cov_thresh);

	void apply_reset_to_all_edges(dBNode* node, Orientation or, Nucleotide nuc)
	{
	  int j;
	  for (j=0; j<NUMBER_OF_BITFIELDS_IN_BINARY_KMER; j++)
	    {
	      reset_one_edge(node, or, nuc, individual_edge_array, j);
	    }
	}
	void apply_reset_to_all_edges_2(dBNode* node )
	{
	  int j;
	  for (j=0; j<NUMBER_OF_BITFIELDS_IN_BINARY_KMER; j++)
	    {
	      db_node_reset_edges(node, individual_edge_array, j);
	    }
	}

	db_graph_remove_low_coverage_nodes(low_cov_thresh,db_graph, &element_get_covg_union_of_all_covgs, &element_get_colour_union_of_all_colours,
					   &apply_reset_to_all_edges, &apply_reset_to_all_edges_2);
	    
	printf("dumping graph %s\n",dumped_binary);
	db_graph_dump_binary(dumped_binary,&db_node_check_status_not_pruned,db_graph);

	break;
      }
    case 2:
      {

	void print_no_extra_info(VariantBranchesAndFlanks* var, FILE* fout)
	{
        }


	//detect variants looking for bubbles in the union graph of all colours.
	//apply no condition to whether one branch or other shoud be one colour or another
	int max_allowed_branch_len=50000; //5000
	db_graph_detect_vars(stdout, max_allowed_branch_len,db_graph, 
			     &detect_vars_condition_flanks_at_least_3,
			     &db_node_action_set_status_visited,
			     &db_node_action_set_status_visited,
			     &element_get_colour_union_of_all_colours, &element_get_covg_union_of_all_covgs,
			     &print_no_extra_info);
	break;
      }
    case 3:
      {
	printf("Print supernodes containing entirely novel(non-reference) sequence\n");
	read_all_ref_chromosomes_and_mark_graph(db_graph);
	int min_covg=2; //demand coverage of at least 2
	
	//todo - add condition to make sure ignore pruned
	printf("Start printing..\n");
	db_graph_print_supernodes_where_condition_is_true_for_all_nodes_in_supernode(db_graph, &db_node_check_status_is_not_exists_in_reference,min_covg, stdout, false, NULL, 0,
										     individual_edge_array,0);//assumes interested in person in colour 0
	
	//cleanup - the printing process has set all printed nodes to visited
	//        - or to visited_and_exists_in_reference
	hash_table_traverse(&db_node_action_unset_status_visited_or_visited_and_exists_in_reference, db_graph); 
	
      break;
      
      }      
    case 4:
      {
	printf("Print supernodes that match reference at start and end, but not the middle\n");
	read_all_ref_chromosomes_and_mark_graph(db_graph);
	int minimum_covg=5; //demand covg of at least 5
	printf("Start printing..\n");
	db_graph_print_supernodes_where_condition_is_true_at_start_and_end_but_not_all_nodes_in_supernode(db_graph, &db_node_check_status_exists_in_reference, minimum_covg,
													  2,25,40, stdout, false,NULL,0, individual_edge_array,0);//assumed interested in colour 0
	//2,25 are required numbers of overlapping nodes with ref at start and end
	//40 is min number of nodes which are NOT in reference
	//cleanup - the printing process has set all printed nodes to visited
	//           - or to visited_and_exists_in_reference
	hash_table_traverse(&db_node_action_unset_status_visited_or_visited_and_exists_in_reference, db_graph); 
	break;
	
      }
    case 5:
      {
	printf("Given this list of fastq files %s, read them each and dump a fasta file for each one containing only those reads lying inside our (cleaned) graph\n", argv[8]);
	
	
	int file_reader(FILE * fp, Sequence * seq, int max_read_length, boolean new_entry, boolean * full_entry){
	  * full_entry = true;
	  
	  if (new_entry!= true){
	    puts("new_entry has to be true for fastq\n");
	    exit(1);
	  }
	  
	  return read_sequence_from_fastq(fp,seq,max_read_length);
	}
	
	
	FILE* list_fptr = fopen(argv[8], "r");
	if (list_fptr==NULL)
	  {
	    printf("Cannot open %s\n", argv[8]);
	    exit(1);
	  }
	
	char line[MAX_FILENAME_LENGTH+1];
	
	
	
	while(fgets(line,MAX_FILENAME_LENGTH, list_fptr) !=NULL)
	  {
	    
	    //remove newline from end of line - replace with \0
	    char* p;
	    if ((p = strchr(line, '\n')) != NULL)
	      {
		*p = '\0';
	      }
	    
	    printf("Looking at %s\n", line);
	    
	    char outputfile[200];
	    sprintf(outputfile,"%s.cleaned.fasta",line);
	    FILE* out = fopen(outputfile, "w");
	    if (out ==NULL)
	      {
		printf("Cannot open %s, exiting", outputfile);
		exit(1);
	      }
	    printf("We will output to %s\n", outputfile);
	    
	    
	    FILE* read_fptr = fopen(line, "r");
	    if (read_fptr==NULL)
	      {
		printf("Cannot open %s", line);
		exit(1);
	      }
	    
	    long long b_reads=0; //will ignore base reads, but needed by API
	    int max_read_length=10000;
	    
	    read_fastq_and_print_reads_that_lie_in_graph(read_fptr, out, &file_reader, 
							 &b_reads, max_read_length, db_graph,
							 false, NULL, NULL);//last 3 arguments show is not for testing
	    fclose(out);
	    fclose(read_fptr);
	    
	  }
	fclose(list_fptr);
	break;
	
      }      
    case 6:
      {
      printf("Given this list of fastq files %s, read them each and dump a fasta file of sub_reads lying inside our (cleaned) graph. Break when a kmer is not in the graph or an edge is not\n", argv[8]);


      int file_reader(FILE * fp, Sequence * seq, int max_read_length, boolean new_entry, boolean * full_entry){
	* full_entry = true;
	
	if (new_entry!= true){
	  puts("new_entry has to be true for fastq\n");
	  exit(1);
	}
	
	return read_sequence_from_fastq(fp,seq,max_read_length);
      }
      

      FILE* list_fptr = fopen(argv[8], "r");
      if (list_fptr==NULL)
	{
	  printf("Cannot open %s\n", argv[8]);
	  exit(1);
	}
      
      char line[MAX_FILENAME_LENGTH+1];
      

      
      while(fgets(line,MAX_FILENAME_LENGTH, list_fptr) !=NULL)
	{
	  
	  //remove newline from end of line - replace with \0
	  char* p;
	  if ((p = strchr(line, '\n')) != NULL)
	    {
	      *p = '\0';
	    }

	  printf("Looking at %s\n", line);

	  char outputfile[200];
	  sprintf(outputfile,"%s.cleaned.fasta",line);
	  FILE* out = fopen(outputfile, "w");
	  if (out ==NULL)
	    {
	      printf("Cannot open %s, exiting", outputfile);
	      exit(1);
	    }
	  printf("We will output to %s\n", outputfile);


	  FILE* read_fptr = fopen(line, "r");
	  if (read_fptr==NULL)
	    {
	      printf("Cannot open %s", line);
	      exit(1);
	    }

	  long long b_reads=0; //will ignore base reads, but needed by API
	  int max_read_length=10000;
	  


	  read_fastq_and_print_subreads_that_lie_in_graph_breaking_at_edges_or_kmers_not_in_graph(read_fptr, out, &file_reader, 
												  &b_reads, max_read_length, db_graph,
												  individual_edge_array, 0, //assumes interested in colour 0
												  false, NULL, NULL);//last 3 arguments show is not for testing
	  fclose(out);
	  fclose(read_fptr);
	  
	}
      fclose(list_fptr);

      break;

      }
    case 7:
      {
	//Make bubble calls for homozygous non-ref variants, using two colours (ref=0, individual=1)
	
	void print_no_extra_info(VariantBranchesAndFlanks* var, FILE* fout)
	{
        }



	boolean condition_is_hom_nonref(VariantBranchesAndFlanks* var)
	{
	  //Assumes the reference is colour 0 and the individual is colour 1
	  int covg_threshold = 1;
	  int i;
	  int count_how_many_nodes_in_one_allele_have_covg_by_indiv=0;
	  int count_how_many_nodes_in_other_allele_have_covg_by_indiv=0;
	  int count_how_many_nodes_in_one_allele_have_covg_by_ref=0;
	  int count_how_many_nodes_in_other_allele_have_covg_by_ref=0;

	  for (i=0; i< var->len_one_allele; i++)
	    {
	      if ( (var->one_allele)[i]->coverage[1] >= covg_threshold )
		{
		  count_how_many_nodes_in_one_allele_have_covg_by_indiv++;
		}
	      if ( (var->one_allele)[i]->coverage[0] > 0 )
		{
		  count_how_many_nodes_in_one_allele_have_covg_by_ref++;
		}
	    }
	  for (i=0; i< var->len_other_allele; i++)
	    {
	      if ( (var->other_allele)[i]->coverage[1] >= covg_threshold )
		{
		  count_how_many_nodes_in_other_allele_have_covg_by_indiv++;
		}
	      if ( (var->other_allele)[i]->coverage[0] > 0 )
		{
		  count_how_many_nodes_in_other_allele_have_covg_by_ref++;
		}
	    }
	  if (//individual has branch1 but not branch2 
	      (count_how_many_nodes_in_one_allele_have_covg_by_indiv==var->len_one_allele)
	       &&
	      (count_how_many_nodes_in_other_allele_have_covg_by_indiv<=1)//last node of the two branches is same
	       &&
	      //reference has branch2 only
	      (count_how_many_nodes_in_one_allele_have_covg_by_ref<=1)//Mario - do you agree?
	      &&
	      (count_how_many_nodes_in_other_allele_have_covg_by_ref==var->len_other_allele)
	      )
	    {
	      return true;
	    }
	  else if (//individual has branch2 but not branch1
		   (count_how_many_nodes_in_one_allele_have_covg_by_indiv<=1)
		   &&
		   (count_how_many_nodes_in_other_allele_have_covg_by_indiv==var->len_one_allele)
		   &&
		   //reference has branch1 only
		   (count_how_many_nodes_in_one_allele_have_covg_by_ref==var->len_other_allele)
		   &&
		   (count_how_many_nodes_in_other_allele_have_covg_by_ref<=1)
		   )
	    {
	      return true;
	    }
	  else
	    {
	      return false;
	    }
		   
	}

	int max_allowed_branch_len=500; 
	db_graph_detect_vars(stdout, max_allowed_branch_len,db_graph, &condition_is_hom_nonref,
			     &db_node_action_set_status_visited,
			     &db_node_action_set_status_visited,
			     &element_get_colour_union_of_all_colours, &element_get_covg_union_of_all_covgs, &print_no_extra_info);

	break;
      }
    case 8:
      {
	printf("Make SV calls based on the trusted-path algorithm, against the whole human genome\n");

	
	char** ref_chroms = malloc( sizeof(char*) * 25); //one for each chromosome, ignoring haplotypes like MHC
	if (ref_chroms==NULL)
	  {
	    printf("OOM. Give up can't even allocate space for the names of the ref chromosome files\n");
	    exit(1);
	  }
	int i;
	for (i=0; i< 25; i++)
	  {
	    ref_chroms[i] = malloc(sizeof(char)*150); //filenames including path are about 100 characters. 50 characters of leeway
	    if (ref_chroms[i]==NULL)
	      {
		printf("OOM. Giveup can't even allocate space for the names of the ref chromosome file i = %d\n",i);
		exit(1);
	      }
	  }
	
	set_ref_chromosome_file_pointers(ref_chroms, 25);
	
	
	// **** set up one output file per chromosome ***** //
	
	char** output_files = malloc( sizeof(char*) * 25); //one for each chromosome, ignoring haplotypes like MHC
	if (output_files==NULL)
	  {
	    printf("OOM. Give up can't even allocate space for the names of the output  files \n");
	    exit(1);
	  }
	
	for (i=0; i< 25; i++)
	  {
	    output_files[i] = malloc(sizeof(char)*100); 
	    if (output_files[i]==NULL)
	      {
		printf("OOM. Giveup can't even allocate space for the names of the ref chromosome file i = %d\n",i);
		exit(1);
	      }
	  }
	
	output_files[0] = "sv_called_in_MT";
	
	for (i=1; i<23; i++)
	  {
	    sprintf(output_files[i], "sv_called_in_chrom_%i", i);
	  }
	output_files[23]="sv_called_in_chrom_X";
	output_files[24]="sv_called_in_chrom_Y";
	
	
	int min_fiveprime_flank_anchor = 2;
	int min_threeprime_flank_anchor= 21;
	int max_anchor_span =  20000;
	int length_of_arrays = 40000;
	int min_covg =1;
	int max_covg = 10000000;
	int max_expected_size_of_supernode=20000;
	
      
	//ignore mitochondrion for now, so start with i=1
	for (i=1; i<25; i++) 
	  {
	    printf("Call SV comparing individual with chromosome %s\n", ref_chroms[i]);
	    
	    FILE* chrom_fptr = fopen(ref_chroms[i], "r");
	    if (chrom_fptr==NULL)
	      {
		printf("Cannot open %s \n", ref_chroms[i]);
		exit(1);
	      }
	    
	    FILE* out_fptr = fopen(output_files[i], "w");
	    if (out_fptr==NULL)
	      {
		printf("Cannot open %s for output\n", output_files[i]);
		exit(1);
	      }
	    
	    //Note we assume person 0 is the reference, and person 1 is the person we are interested in
	    db_graph_make_reference_path_based_sv_calls(chrom_fptr, individual_edge_array, 1, 
							individual_edge_array, 0,
							min_fiveprime_flank_anchor, min_threeprime_flank_anchor, max_anchor_span, min_covg, max_covg, 
							max_expected_size_of_supernode, length_of_arrays, db_graph, out_fptr,
							0, NULL, NULL, NULL, NULL, NULL, &make_reference_path_based_sv_calls_condition_always_true, &db_variant_action_do_nothing);
	    
	    
	    fclose(chrom_fptr);
	    fclose(out_fptr);
	  }
	//cleanup
	for(i=0; i<25; i++)
	  {
	    free(ref_chroms[i]);
	    free(output_files[i]);
	  }
	free(ref_chroms);
	free(output_files);
	break;
	
      }
    case 9:
      {


	void print_no_extra_info(VariantBranchesAndFlanks* var, FILE* fout)
	{
        }
	
	int get_covg_ref(dBNode* e)
	{
	  return e->coverage[0];
	}
	int get_covg_indiv(dBNode* e)
	{
	  return e->coverage[1];
	}

	boolean detect_vars_condition_is_hom_nonref(VariantBranchesAndFlanks* var)
	{
	  return detect_vars_condition_is_hom_nonref_given_colour_funcs_for_ref_and_indiv(var, &get_covg_ref, &get_covg_indiv);
	}




	// STEP1 - detect vars, calls hets, with no conditions

	int max_allowed_branch_len=50000; 
	FILE* detect_vars_fptr = fopen(detectvars_filename, "w");
	if (detect_vars_fptr==NULL)
	  {
	    printf("Cannot open %s, so exit\n", detectvars_filename);
	    exit(1);
	  }
	printf("Going to output ref free hets to %s\n", detectvars_filename);
	db_graph_detect_vars(detect_vars_fptr, max_allowed_branch_len,db_graph, &detect_vars_condition_always_true,
			     &db_node_action_set_status_visited,
			     &db_node_action_set_status_visited,
			     &element_get_colour1, &element_get_covg_colour1, &print_no_extra_info);
	fclose(detect_vars_fptr);

	//cleanu
	hash_table_traverse(&db_node_action_unset_status_visited_or_visited_and_exists_in_reference, db_graph);	


	//STEP2 - detect vars in the reference colour, and mark these branches as visited, so they are ignored. Then call vars in colour1
	FILE* detect_vars_after_remv_ref_bub_fptr = fopen(detectvars_after_remv_ref_bubble_filename, "w");
	if (detect_vars_after_remv_ref_bub_fptr==NULL)
	  {
	    printf("Cannot open %s so exit\n", detectvars_after_remv_ref_bubble_filename);
	    exit(1);
	  }
	printf("Call het variants after marking off the bubbles in the ref\n");
	db_graph_detect_vars_after_marking_vars_in_reference_to_be_ignored(detect_vars_after_remv_ref_bub_fptr, max_allowed_branch_len,db_graph, 
									   &detect_vars_condition_always_true,
									   &element_get_colour0, &element_get_covg_colour0,
									   &element_get_colour1, &element_get_covg_colour1, &print_no_extra_info);
	fclose(detect_vars_after_remv_ref_bub_fptr);
	//no need to traverse and do cleanup, as db_graph_detect_vars_after_marking_vars_in_reference_to_be_ignored does it at the end

	//STEP3 - detect hom non ref variants
	FILE* detect_vars_hom_nonref_fptr = fopen(detectvars_hom_nonref_filename, "w");
	if (detect_vars_hom_nonref_fptr==NULL)
	  {
	    printf("Cannot open %s so exit\n", detectvars_hom_nonref_filename);
	    exit(1);
	  }

	printf("About to print hom nonref calls to %s\n", detectvars_hom_nonref_filename);
	db_graph_detect_vars( detect_vars_hom_nonref_fptr, max_allowed_branch_len,db_graph, &detect_vars_condition_is_hom_nonref,
			      &db_node_action_set_status_visited,  &db_node_action_set_status_visited,
			      &element_get_colour_union_of_all_colours, &element_get_covg_union_of_all_covgs, &print_no_extra_info);

	//cleanup
	hash_table_traverse(&db_node_action_set_status_none, db_graph);	


	//STEP 4 - detect homovariants using ref-assisted trusted-path algorithm

	int min_fiveprime_flank_anchor = 3;
	int min_threeprime_flank_anchor= 3;
	int max_anchor_span =  70000;
	int length_of_arrays = 140000;
	int min_covg =1;
	int max_covg = 100000000;
	int max_expected_size_of_supernode=70000;
	

	//needs a filepointer to traverse the reference as it walks the graph
	printf("Detect polymorphic sites using reference assisted caller, using this ref file %s\n", ref_fasta);

	FILE* ref_ass_fptr = fopen(ref_assisted_filename, "w");
	if (ref_ass_fptr==NULL)
	  {
	    printf("Cnnot open %s, so exit", ref_assisted_filename);
	    exit(1);
	  }

	FILE* ref_fptr = fopen(ref_fasta, "r");
	if (ref_fptr==NULL)
	  {
	    printf("Cannot open %s, so exit", ref_fasta);
	    exit(1);
	  }

	db_graph_make_reference_path_based_sv_calls(ref_fptr, individual_edge_array, 1, individual_edge_array, 0,
						    min_fiveprime_flank_anchor, min_threeprime_flank_anchor, max_anchor_span, min_covg, max_covg, 
						    max_expected_size_of_supernode, length_of_arrays, db_graph, ref_ass_fptr,
						    0, NULL, NULL, NULL, NULL, NULL, &make_reference_path_based_sv_calls_condition_always_true, &db_variant_action_do_nothing);
	fclose(ref_ass_fptr);


	//cleanup
	hash_table_traverse(&db_node_action_unset_status_visited_or_visited_and_exists_in_reference, db_graph);	


	printf("Finished making all calls\n");

	break;
      }
    case 10:
	{
	  /*
	  int get_covg_ref(dBNode* e)
	  {
	    return e->coverage[0];
	  }
	  int get_covg_na12878(dBNode* e)
	  {
	    return e->coverage[1];
	  }
	  int get_covg_na19240(dBNode* e)
	  {
	    return e->coverage[2];
	  }
	  int get_covg_chimp(dBNode* e)
	  {
	    return e->coverage[3];
	  }
	  int get_covg_gorilla(dBNode* e)
	  {
	    return e->coverage[4];
	  }
	  int get_covg_macaca(dBNode* e)
	  {
	    return e->coverage[5];
	  }
	  
	  
	  boolean detect_vars_condition_is_hom_nonref(VariantBranchesAndFlanks* var)
	  {
	    return detect_vars_condition_is_hom_nonref_given_colour_funcs_for_ref_and_indiv(var, &get_covg_ref, &get_covg_indiv);
	  }



	void print_extra_info(VariantBranchesAndFlanks* var, FILE* fout)
	{
	  // determine ancestral allele by comparing with chimp, gorilla, macaca
	  // determine which individual this variant is on, and print
        }
	







	// STEP1 - detect vars, calls hets, with no conditions

	int max_allowed_branch_len=50000; 
	FILE* detect_vars_fptr = fopen(detectvars_filename, "w");
	if (detect_vars_fptr==NULL)
	  {
	    printf("Cannot open %s, so exit\n", detectvars_filename);
	    exit(1);
	  }
	printf("Going to output ref free hets to %s\n", detectvars_filename);
	db_graph_detect_vars(detect_vars_fptr, max_allowed_branch_len,db_graph, &detect_vars_condition_always_true,
			     &db_node_action_set_status_visited,
			     &db_node_action_set_status_visited,
			     &element_get_colour1, &element_get_covg_colour1);
	fclose(detect_vars_fptr);

	//cleanu
	hash_table_traverse(&db_node_action_unset_status_visited_or_visited_and_exists_in_reference, db_graph);	


	//STEP2 - detect vars in the reference colour, and mark these branches as visited, so they are ignored. Then call vars in colour1
	FILE* detect_vars_after_remv_ref_bub_fptr = fopen(detectvars_after_remv_ref_bubble_filename, "w");
	if (detect_vars_after_remv_ref_bub_fptr==NULL)
	  {
	    printf("Cannot open %s so exit\n", detectvars_after_remv_ref_bubble_filename);
	    exit(1);
	  }
	printf("Call het variants after marking off the bubbles in the ref\n");
	db_graph_detect_vars_after_marking_vars_in_reference_to_be_ignored(detect_vars_after_remv_ref_bub_fptr, max_allowed_branch_len,db_graph, 
									   &detect_vars_condition_always_true,
									   &element_get_colour0, &element_get_covg_colour0,
									   &element_get_colour1, &element_get_covg_colour1);
	fclose(detect_vars_after_remv_ref_bub_fptr);
	//no need to traverse and do cleanup, as db_graph_detect_vars_after_marking_vars_in_reference_to_be_ignored does it at the end

	//STEP3 - detect hom non ref variants
	FILE* detect_vars_hom_nonref_fptr = fopen(detectvars_hom_nonref_filename, "w");
	if (detect_vars_hom_nonref_fptr==NULL)
	  {
	    printf("Cannot open %s so exit\n", detectvars_hom_nonref_filename);
	    exit(1);
	  }

	printf("About to print hom nonref calls to %s\n", detectvars_hom_nonref_filename);
	db_graph_detect_vars( detect_vars_hom_nonref_fptr, max_allowed_branch_len,db_graph, &detect_vars_condition_is_hom_nonref,
			      &db_node_action_set_status_visited,  &db_node_action_set_status_visited,
			      &element_get_colour_union_of_all_colours, &element_get_covg_union_of_all_covgs);

	//cleanup
	hash_table_traverse(&db_node_action_set_status_none, db_graph);	


	//STEP 4 - detect homovariants using ref-assisted trusted-path algorithm

	int min_fiveprime_flank_anchor = 3;
	int min_threeprime_flank_anchor= 3;
	int max_anchor_span =  70000;
	int length_of_arrays = 140000;
	int min_covg =1;
	int max_covg = 100000000;
	int max_expected_size_of_supernode=70000;
	

	//needs a filepointer to traverse the reference as it walks the graph
	printf("Detect polymorphic sites using reference assisted caller, using this ref file %s\n", ref_fasta);

	FILE* ref_ass_fptr = fopen(ref_assisted_filename, "w");
	if (ref_ass_fptr==NULL)
	  {
	    printf("Cnnot open %s, so exit", ref_assisted_filename);
	    exit(1);
	  }

	FILE* ref_fptr = fopen(ref_fasta, "r");
	if (ref_fptr==NULL)
	  {
	    printf("Cannot open %s, so exit", ref_fasta);
	    exit(1);
	  }

	db_graph_make_reference_path_based_sv_calls(ref_fptr, individual_edge_array, 1, individual_edge_array, 0,
						    min_fiveprime_flank_anchor, min_threeprime_flank_anchor, max_anchor_span, min_covg, max_covg, 
						    max_expected_size_of_supernode, length_of_arrays, db_graph, ref_ass_fptr,
						    0, NULL, NULL, NULL, NULL, NULL, &make_reference_path_based_sv_calls_condition_always_true, &db_variant_action_do_nothing);
	fclose(ref_ass_fptr);


	//cleanup
	hash_table_traverse(&db_node_action_unset_status_visited_or_visited_and_exists_in_reference, db_graph);	


	printf("Finished making all calls\n");
	  */	  
	  break;
	}
      
      

    }

  hash_table_free(&db_graph);

  return 0;
}
