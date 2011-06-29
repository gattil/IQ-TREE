//
// C++ Implementation: alignment
//
// Description: 
//
//
// Author: BUI Quang Minh, Steffen Klaere, Arndt von Haeseler <minh.bui@univie.ac.at>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "alignment.h"
#include "myreader.h"

char symbols_protein[] = "ARNDCQEGHILKMFPSTWYVX"; // X for unknown AA
char symbols_dna[]     = "ACGT";
char symbols_rna[]     = "ACGU";
char symbols_binary[]  = "01";

Alignment::Alignment()
 : vector<Pattern>()
{
	num_states = 0;
	frac_const_sites = 0.0;
}

string &Alignment::getSeqName(int i) {
	assert(i >= 0 && i < seq_names.size());
	return seq_names[i];
}

int Alignment::getSeqID(string &seq_name) {
	for (int i = 0; i < getNSeq(); i++)
		if (seq_name == getSeqName(i)) return i;
	return -1;
}

int Alignment::getMaxSeqNameLength() {
	int len = 0;
	for (int i = 0; i < getNSeq(); i++)
		if (getSeqName(i).length() > len) 
			len = getSeqName(i).length();
	return len;
}

void Alignment::checkSeqName() {
	ostringstream warn_str;
	StrVector::iterator it;
	for (it = seq_names.begin(); it != seq_names.end(); it++) {
		string orig_name = (*it);
		for (string::iterator i = it->begin(); i != it->end(); i++) {
			if (!isalnum(*i) && (*i) != '_' && (*i) != '-' && (*i) != '.') {
				(*i) = '_';
			}
		}
		if (orig_name != (*it)) 
			warn_str << orig_name << " -> " << (*it) << endl;
	}
	if (warn_str.str() != "") {
		string str = "Some sequence names are changed as follows:\n";
		outWarning(str + warn_str.str());
	}
	// now check that sequence names are different
	StrVector names;
	names.insert(names.begin(), seq_names.begin(), seq_names.end());
	sort(names.begin(), names.end());
	bool ok = true;
	for (it = names.begin(); it != names.end(); it++) {
		if (it+1==names.end()) break;
		if (*it == *(it+1)) {
			cout << "ERROR: Duplicated sequence name " << *it << endl;
			ok = false;
		}
	}
	if (!ok) outError("Please rename sequences listed above!");
	if (verbose_mode >= VB_MIN) {
		int max_len = getMaxSeqNameLength();
		cout.width(max_len);
		cout << left << "Name" << " #Ungappy+unambiguous chars" << endl;
		int num_problem_seq = 0;
		for (int i = 0; i < seq_names.size(); i++) {
			int num_proper_chars = countProperChar(i);
			int percent_proper_chars = num_proper_chars*100 / getNSite();
			cout.width(max_len);
			cout << left << seq_names[i] << " " << num_proper_chars << " (" << percent_proper_chars << "%)" << endl;
			if (percent_proper_chars < 50) num_problem_seq++;
		}
		if (num_problem_seq) cout << "WARNING: " << num_problem_seq << " sequences contain less than 50% proper characters" << endl;
	}
}

Alignment::Alignment(char *filename, char *sequence_type, InputType &intype) : vector<Pattern>() {

	cout << "Reading alignment file " << filename << " ..." << endl;
	intype = detectInputFile(filename);

	try {
	
		if (intype == IN_NEXUS) {
			cout << "Nexus format detected" << endl;
			readNexus(filename);
		} else if (intype == IN_FASTA) {
			cout << "Fasta format detected" << endl;
			readFasta(filename, sequence_type);
		} else if (intype == IN_PHYLIP) {
			cout << "Phylip format detected" << endl;
			readPhylip(filename, sequence_type);
		} else {
			outError("Unknown sequence format, please use PHYLIP, FASTA, or NEXUS format");
		}
	} catch (ios::failure) {
		outError(ERR_READ_INPUT);
	} catch (const char *str) {
		outError(str);
	} catch (string str) {
		outError(str);
	}

	if (getNSeq() < 3) 
		outError("Alignment must have at least 3 sequences");
		
	checkSeqName();
	cout << "Alignment contains " << getNSeq() << " sequences with " << getNSite() << 
		" characters and " << getNPattern() << " patterns"<< endl;
	//cout << "Number of character states is " << num_states << endl;
	//cout << "Number of patterns = " << size() << endl;
	countConstSite();
	//cout << "Fraction of constant sites: " << frac_const_sites << endl;

}

int Alignment::readNexus(char *filename) {
	NxsTaxaBlock *taxa_block;
	NxsAssumptionsBlock *assumptions_block;
	NxsDataBlock *data_block = NULL;
	NxsTreesBlock *trees_block = NULL;

	taxa_block = new NxsTaxaBlock();
	assumptions_block = new NxsAssumptionsBlock(taxa_block);
	data_block = new NxsDataBlock(taxa_block, assumptions_block);
	trees_block = new TreesBlock(taxa_block);

	MyReader nexus(filename);

	nexus.Add(taxa_block);
	nexus.Add(assumptions_block);
	nexus.Add(data_block);
	nexus.Add(trees_block);

	MyToken token(nexus.inf);
	nexus.Execute(token);

	if (data_block->GetNTax() == 0) {
		outError("No data is given in the input file");	
		return 0;
	}
	if (verbose_mode >= VB_DEBUG)
		data_block->Report(cout);
	
	extractDataBlock(data_block);

	return 1;
}

void Alignment::extractDataBlock(NxsCharactersBlock *data_block) {
	int nseq = data_block->GetNTax();
	int nsite = data_block->GetNCharTotal();
	char *symbols = NULL;
	//num_states = strlen(symbols);
	char char_to_state[NUM_CHAR];
	char state_to_char[NUM_CHAR];

	NxsCharactersBlock::DataTypesEnum data_type = (NxsCharactersBlock::DataTypesEnum)data_block->GetDataType();
	if (data_type == NxsCharactersBlock::continuous) {
		outError("Continuous characters not supported");
	} else if (data_type == NxsCharactersBlock::dna || data_type == NxsCharactersBlock::rna || 
		data_type == NxsCharactersBlock::nucleotide) 
	{
		num_states = 4;
		if (data_type == NxsCharactersBlock::rna) 
			symbols = symbols_rna;
		else
			symbols = symbols_dna;
	} else if (data_type == NxsCharactersBlock::protein) {
		num_states = 20;
		symbols = symbols_protein;
	} else {
		num_states = 2;
		symbols = symbols_binary;
	}

	memset(char_to_state, STATE_UNKNOWN, NUM_CHAR);
	memset(state_to_char, '?', NUM_CHAR);
	for (int i = 0; i < strlen(symbols); i++) {
		char_to_state[(int)symbols[i]] = i;	
		state_to_char[i] = symbols[i];
	}
	state_to_char[(int)STATE_UNKNOWN] = '-';


	int seq, site;

	for (seq = 0; seq < nseq; seq++) {
		seq_names.push_back(data_block->GetTaxonLabel(seq));
	}

	site_pattern.resize(nsite, -1);

	int num_gaps_only = 0;

	for (site = 0; site < nsite; site++) {
 		Pattern pat;
		for (seq = 0; seq < nseq; seq++) {
			int nstate = data_block->GetNumStates(seq, site);
			if (nstate == 0) 
				pat += STATE_UNKNOWN;
			else if (nstate == 1) {
				pat += char_to_state[(int)data_block->GetState(seq, site, 0)];
			} else {
				assert(data_type != NxsCharactersBlock::dna || data_type != NxsCharactersBlock::rna || data_type != NxsCharactersBlock::nucleotide);
				char pat_ch = 0;
				for (int state = 0; state < nstate; state++) {
					pat_ch |= (1 << char_to_state[(int)data_block->GetState(seq, site, state)]);
				}
				pat_ch += 3;
				pat += pat_ch;
			} 
		}
		num_gaps_only += addPattern(pat, site);
	}
	if (num_gaps_only)
		cout << "WARNING: " << num_gaps_only << " sites contain only gaps or ambiguous chars." << endl;
	if (verbose_mode >= VB_MAX)
		for (site = 0; site < size(); site++) {
			for (seq = 0; seq < nseq; seq++)
				cout << state_to_char[(int)(*this)[site][seq]];
 			cout << "  " << (*this)[site].frequency << endl;
		}
}

bool Alignment::addPattern(Pattern &pat, int site, int freq) {
	// check if pattern contains only gaps
	bool gaps_only = true;
	for (Pattern::iterator it = pat.begin(); it != pat.end(); it++)
		if ((*it) != STATE_UNKNOWN) { 
			gaps_only = false; 
			break;
		}
	if (gaps_only) {
		if (verbose_mode >= VB_DEBUG)
			cout << "Site " << site << " contains only gaps or ambiguous characters" << endl;
		//return true;
	}
	PatternIntMap::iterator pat_it = pattern_index.find(pat);
	if (pat_it == pattern_index.end()) { // not found
		pat.frequency = freq;
		pat.computeConst();
		push_back(pat);
		pattern_index[pat] = size()-1;
		site_pattern[site] = size()-1;
	} else {
		int index = pat_it->second;
		at(index).frequency += freq;
		site_pattern[site] = index;
	} 
	return gaps_only;
}

/**
	detect the data type of the input sequences
	@param sequences vector of strings
	@return the data type of the input sequences
*/
SeqType Alignment::detectSequenceType(StrVector &sequences) {
	int num_nuc = 0;
	int num_ungap = 0;
	int num_bin = 0;
	int num_alphabet = 0;

	for (StrVector::iterator it = sequences.begin(); it != sequences.end(); it++)
		for (string::iterator i = it->begin(); i != it->end(); i++) {
			if ((*i) != '?' && (*i) != '-' && (*i) != '.') num_ungap++;
			if ((*i) == 'A' || (*i) == 'C' || (*i) == 'G' || (*i) == 'T' || (*i) == 'U')
				num_nuc++;
			if ((*i) == '0' || (*i) == '1')
				num_bin++;
			if (isalpha(*i)) num_alphabet++;
		}
	if (((double)num_nuc) / num_ungap > 0.9)
		return SEQ_DNA;
	if (((double)num_bin) / num_ungap > 0.9)
		return SEQ_BINARY;
	if (((double)num_alphabet) / num_ungap < 0.5)
		return SEQ_UNKNOWN;
	return SEQ_PROTEIN;
}

void buildStateMap(char *map, SeqType seq_type) {
	memset(map, STATE_INVALID, NUM_CHAR);
	map['?'] = STATE_UNKNOWN;
	map['-'] = STATE_UNKNOWN;
	map['.'] = STATE_UNKNOWN;

	switch (seq_type) {
	case SEQ_BINARY:
		map['0'] = 0;
		map['1'] = 1;
		return;
	case SEQ_DNA: // DNA
			map['A'] = 0;
			map['C'] = 1;
			map['G'] = 2;
			map['T'] = 3;
			map['U'] = 3;
			map['R'] = 1+4+3; // A or G, Purine
			map['Y'] = 2+8+3; // C or T, Pyrimidine
			map['N'] = STATE_UNKNOWN;
			map['W'] = 1+8+3; // A or T, Weak
			map['S'] = 2+4+3; // G or C, Strong
			map['M'] = 1+2+3; // A or C, Amino
			map['K'] = 4+8+3; // G or T, Keto
			map['B'] = 2+4+8+3; // C or G or T
			map['H'] = 1+2+8+3; // A or C or T
			map['D'] = 1+4+8+3; // A or G or T
			map['V'] = 1+2+4+3; // A or G or C
		return;
	case SEQ_PROTEIN: // Protein
		for (int i = 0; i < 20; i++)
			map[(int)symbols_protein[i]] = i;
		map[(int)symbols_protein[20]] = STATE_UNKNOWN;
		return;
	case SEQ_MULTISTATE:
		for (int i = 0; i <= STATE_UNKNOWN; i++)
			map[i] = i;
		return;
	default:
		return;
	}
}


/**
	convert a raw characer state into ID, indexed from 0
	@param state input raw state
	@param seq_type data type (SEQ_DNA, etc.)
	@return state ID
*/
char Alignment::convertState(char state, SeqType seq_type) {
	if (state == '?' || state == '-' || state == '.')
		return STATE_UNKNOWN;

	char *loc;

	switch (seq_type) {
	case SEQ_BINARY:
		switch (state) {
		case '0': return 0;
		case '1': return 1;
		default: return STATE_INVALID;
		}
	case SEQ_DNA: // DNA
		switch (state) {
			case 'A': return 0;
			case 'C': return 1;
			case 'G': return 2;
			case 'T': return 3;
			case 'U': return 3;
			case 'R': return 1+4+3; // A or G, Purine
			case 'Y': return 2+8+3; // C or T, Pyrimidine
			case 'N': return STATE_UNKNOWN;
			case 'W': return 1+8+3; // A or T, Weak
			case 'S': return 2+4+3; // G or C, Strong
			case 'M': return 1+2+3; // A or C, Amino
			case 'K': return 4+8+3; // G or T, Keto
			case 'B': return 2+4+8+3; // C or G or T
			case 'H': return 1+2+8+3; // A or C or T
			case 'D': return 1+4+8+3; // A or G or T
			case 'V': return 1+2+4+3; // A or G or C
			default: return STATE_INVALID; // unrecognize character
		}
		return state;
	case SEQ_PROTEIN: // Protein
		loc = strchr(symbols_protein, state);
		
		if (!loc) return STATE_INVALID; // unrecognize character
		state = loc - symbols_protein;
		if (state < 20) 
			return state;
		else 
			return STATE_UNKNOWN;
	default:
		return STATE_INVALID;
	}
}

char Alignment::convertStateBack(char state) {
	if (state == STATE_UNKNOWN) return '-';
	if (state == STATE_INVALID) return '?';

	switch (num_states) {
	case 2:
		switch (state) {
		case 0: return '0';
		case 1: return '1';
		default: return STATE_INVALID;
		}
	case 4: // DNA
		switch (state) {
			case 0: return 'A';
			case 1: return 'C';
			case 2: return 'G';
			case 3: return 'T';
			case 1+4+3: return 'R'; // A or G, Purine
			case 2+8+3: return 'Y'; // C or T, Pyrimidine
			case 1+8+3: return 'W'; // A or T, Weak
			case 2+4+3: return 'S'; // G or C, Strong
			case 1+2+3: return 'M'; // A or C, Amino
			case 4+8+3: return 'K'; // G or T, Keto
			case 2+4+8+3: return 'B'; // C or G or T
			case 1+2+8+3: return 'H'; // A or C or T
			case 1+4+8+3: return 'D'; // A or G or T
			case 1+2+4+3: return 'V'; // A or G or C
			default: return '?'; // unrecognize character
		}
		return state;
	case 20: // Protein
		if (state < 20) 
			return symbols_protein[(int)state];
		else 
			return '-';
	default:
		return '?';
	}
}

void Alignment::convertStateStr(string &str, SeqType seq_type) {
	for (string::iterator it = str.begin(); it != str.end(); it++)
		(*it) = convertState(*it, seq_type);
}

int Alignment::buildPattern(StrVector &sequences, char *sequence_type, int nseq, int nsite) {
	int seq_id;
	ostringstream err_str;
	site_pattern.resize(nsite, -1);
	clear();
	pattern_index.clear();

	if (nseq != seq_names.size()) throw "Different number of sequences than specified";

	/* now check that all sequence names are correct */
	for (seq_id = 0; seq_id < nseq; seq_id ++) {
		ostringstream err_str;
		if (seq_names[seq_id] == "") 
			err_str << "Sequence number " << seq_id+1 << " has no names\n";
		// check that all the names are different
		for (int i = 0; i < seq_id; i++)
			if (seq_names[i] == seq_names[seq_id])
				err_str << "The sequence name " << seq_names[seq_id] << " is dupplicated\n";
	}
	if (err_str.str() != "")
		throw err_str.str();


	/* now check that all sequences have the same length */
	for (seq_id = 0; seq_id < nseq; seq_id ++) {
		if (sequences[seq_id].length() != nsite) {
			err_str << "Sequence " << seq_names[seq_id] << " contains ";
			if (sequences[seq_id].length() < nsite)
				err_str << "not enough";
			else
				err_str << "too many";
				
			err_str << " characters (" << sequences[seq_id].length() << ")\n";
		}
	}

	if (err_str.str() != "")
		throw err_str.str();

	/* now check data type */		
	SeqType seq_type = SEQ_UNKNOWN;
	seq_type = detectSequenceType(sequences);
	switch (seq_type) {
	case SEQ_BINARY: 
		num_states = 2;
		cout << "Alignment most likely contains binary sequences" << endl;
		break;
	case SEQ_DNA: 
		num_states = 4;
		cout << "Alignment most likely contains DNA/RNA sequences" << endl;
		break;
	case SEQ_PROTEIN:
		num_states = 20;
		cout << "Alignment most likely contains protein sequences" << endl;
		break;
	default: 
		if (!sequence_type)
			throw "Unknown sequence type.";
	}
	SeqType user_seq_type;
	if (sequence_type) {
		if (strcmp(sequence_type, "BIN") == 0) {
			num_states = 2;
			user_seq_type = SEQ_BINARY;
		} else if (strcmp(sequence_type, "DNA") == 0) {
			num_states = 4;
			user_seq_type = SEQ_DNA;
		} else if (strcmp(sequence_type, "AA") == 0) {
			num_states = 20;
			user_seq_type = SEQ_PROTEIN;
		} else if (strcmp(sequence_type, "MULTI") == 0) {
			cout << "Multi-state data with " << num_states << " alphabets" << endl;
			user_seq_type = SEQ_MULTISTATE;
		} else
			throw "Invalid sequence type.";
		if (user_seq_type != seq_type && seq_type != SEQ_UNKNOWN)
			outWarning("Your specified sequence type is different from the detected one");
		seq_type = user_seq_type;
	}

	// now convert to patterns
	int site, seq, num_gaps_only = 0;

	char char_to_state[NUM_CHAR];

	buildStateMap(char_to_state, seq_type);

	Pattern pat;
	pat.resize(nseq);
	for (site = 0; site < nsite; site++) {
		for (seq = 0; seq < nseq; seq++) {
			//char state = convertState(sequences[seq][site], seq_type);
			char state = char_to_state[(int)(sequences[seq][site])];
			if (state == STATE_INVALID) 
				err_str << "Sequence " << seq_names[seq] << " has invalid character " << 
					sequences[seq][site] << " at site " << site+1 << "\n";
			pat[seq] = state;
		}
		num_gaps_only += addPattern(pat, site);
	}
	if (num_gaps_only)
		cout << "WARNING: " << num_gaps_only << " sites contain only gaps or ambiguous chars." << endl;
	if (err_str.str() != "")
		throw err_str.str();
	return 1;
}

int Alignment::readPhylip(char *filename, char *sequence_type) {
	
	StrVector sequences;
	ostringstream err_str;
	ifstream in;
	int line_num = 1;
	// set the failbit and badbit
	in.exceptions(ios::failbit | ios::badbit);
	in.open(filename);
	int nseq = 0, nsite = 0;
	int seq_id = 0;
	string line;
	// remove the failbit
	in.exceptions(ios::badbit);
	bool multi_state = (sequence_type && strcmp(sequence_type,"MULTI") == 0);
	num_states = 0;
	
	for (; !in.eof(); line_num++) {
		getline(in, line);
		if (line == "") continue;

		//cout << line << endl;		
		if (nseq == 0) { // read number of sequences and sites
			istringstream line_in(line);
			if (!(line_in >> nseq >> nsite))
				throw "Invalid PHYLIP format. First line must contain number of sequences and sites";
			//cout << "nseq: " << nseq << "  nsite: " << nsite << endl;
			if (nseq < 3)
				throw "There must be at least 3 sequences";
			if (nsite < 1)
				throw "No alignment columns";

			seq_names.resize(nseq, "");
			sequences.resize(nseq, "");

		} else { // read sequence contents
			if (seq_names[seq_id] == "") { // cut out the sequence name
				string::size_type pos = line.find(' ');
				if (pos == string::npos) pos = 10; //  assume standard phylip
				seq_names[seq_id] = line.substr(0, pos);
				line.erase(0, pos);
			}
			if (multi_state) {
				stringstream linestr(line);
				int state;
				while (!linestr.eof() ) {
					state = -1;
					linestr >> state;
					if (state < 0) break;
					sequences[seq_id].append(1, state);
					if (num_states < state+1) num_states = state+1;
				}
			} else
			for (string::iterator it = line.begin(); it != line.end(); it++) {
				if ((*it) <= ' ') continue;
				if (isalnum(*it) || (*it) == '-' || (*it) == '?'|| (*it) == '.')
					sequences[seq_id].append(1, toupper(*it));
				else {
					err_str << "Unrecognized character " << *it << " on line " << line_num;
					throw err_str.str();
				}
			}
			seq_id++;
			if (seq_id == nseq) seq_id = 0;
		} 
		//sequences.	
	}
	in.clear();
	// set the failbit again
	in.exceptions(ios::failbit | ios::badbit);
	in.close();

	return buildPattern(sequences, sequence_type, nseq, nsite);
}

int Alignment::readFasta(char *filename, char *sequence_type) {
	
	StrVector sequences;
	ostringstream err_str;
	ifstream in;
	int line_num = 1;
	string line;

	// set the failbit and badbit
	in.exceptions(ios::failbit | ios::badbit);
	in.open(filename);
	// remove the failbit
	in.exceptions(ios::badbit);

	for (; !in.eof(); line_num++) {
		getline(in, line);
		if (line == "") continue;

		//cout << line << endl;		
		if (line[0] == '>') { // next sequence
			string::size_type pos = line.find(' ');
			seq_names.push_back(line.substr(1, pos-1));
			sequences.push_back("");
			continue;
		}
		 // read sequence contents
		if (sequences.empty()) throw "First line must begin with '>' to define sequence name";
		for (string::iterator it = line.begin(); it != line.end(); it++) {
			if ((*it) <= ' ') continue;
			if (isalnum(*it) || (*it) == '-' || (*it) == '?'|| (*it) == '.')
				sequences.back().append(1, toupper(*it));
			else {
				err_str << "Unrecognized character " << *it << " on line " << line_num;
				throw err_str.str();
			}
		}
	}
	in.clear();
	// set the failbit again
	in.exceptions(ios::failbit | ios::badbit);
	in.close();

	return buildPattern(sequences, sequence_type, seq_names.size(), sequences.front().length());
}

bool Alignment::getSiteFromResidue(int seq_id, int &residue_left, int &residue_right) {
	int i, j;
	int site_left = -1, site_right = -1;
	for (i = 0, j = -1; i < getNSite(); i++) {
		if (at(site_pattern[i])[seq_id] != STATE_UNKNOWN) j++;
		if (j == residue_left) site_left = i;
		if (j == residue_right-1) site_right = i+1;
	}
	if (site_left < 0 || site_right < 0)
		 cout << "Out of range: Maxmimal residue number is " << j+1 << endl;
	if (site_left == -1) outError("Left residue range is too high");
	if (site_right == -1) {
		outWarning("Right residue range is set to alignment length");
		site_right = getNSite();
	}
	residue_left = site_left;
	residue_right = site_right;
	return true;
}

int Alignment::buildRetainingSites(const char *aln_site_list, IntVector &kept_sites, 
	bool exclude_gaps, const char *ref_seq_name) 
{
	if (aln_site_list) {
		int seq_id = -1;
		if (ref_seq_name) {
			string ref_seq = ref_seq_name;
			seq_id = getSeqID(ref_seq);
			if (seq_id < 0) outError("Reference sequence name not found: ", ref_seq_name);
		}
		cout << "Reading site position list " << aln_site_list << " ..." << endl;
		kept_sites.resize(getNSite(), 0);
		try {
			ifstream in;
			in.exceptions(ios::failbit | ios::badbit);
			in.open(aln_site_list);
			in.exceptions(ios::badbit);
		
			while (!in.eof()) {
				int left, right;
				left = right = 0;
				in >> left;
				if (in.eof()) break;
				in >> right;
				cout << left << "-" << right << endl;
				if (left <= 0 || right <= 0) throw "Range must be positive";
				if (left > right) throw "Left range is bigger than right range";
				left--;
				if (right > getNSite()) throw "Right range is bigger than alignment size";
				if (seq_id >= 0) getSiteFromResidue(seq_id, left, right);
				for (int i = left; i < right; i++)
					kept_sites[i] = 1;
			}
			in.close();
		} catch (ios::failure) {
			outError(ERR_READ_INPUT, aln_site_list);
		} catch (const char* str) {
			outError(str);
		} 
	} else {
		kept_sites.resize(getNSite(), 1);
	}

	int j;
	if (exclude_gaps) {
		for (j = 0; j < kept_sites.size(); j++) 
		if (kept_sites[j] && at(site_pattern[j]).computeAmbiguousChar(num_states) > 0) {
			kept_sites[j] = 0;
		}
	}

	int final_length = 0;
	for (j = 0; j < kept_sites.size(); j++) 
		if (kept_sites[j]) final_length++;
	return final_length;
}

void Alignment::printPhylip(const char *file_name, bool append, const char *aln_site_list, 
	bool exclude_gaps, const char *ref_seq_name) {
	IntVector kept_sites;
	int final_length = buildRetainingSites(aln_site_list, kept_sites, exclude_gaps, ref_seq_name);

	try {
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);

		if (append)
			out.open(file_name, ios_base::out | ios_base::app);
		else
			out.open(file_name);
		out << getNSeq() << " " << final_length << endl;
		StrVector::iterator it;
		int max_len = getMaxSeqNameLength();
		if (max_len < 10) max_len = 10;
		int seq_id = 0;
		for (it = seq_names.begin(); it != seq_names.end(); it++, seq_id++) {
			out.width(max_len);
			out << left << (*it) << "  ";
			int j = 0;
			for (IntVector::iterator i = site_pattern.begin();  i != site_pattern.end(); i++, j++)
				if (kept_sites[j])
				out << convertStateBack(at(*i)[seq_id]);
			out << endl;
		}
		out.close();
		cout << "Alignment was printed to " << file_name << endl;
	} catch (ios::failure) {
		outError(ERR_WRITE_OUTPUT, file_name);
	}	
}

void Alignment::printFasta(const char *file_name, bool append, const char *aln_site_list
	, bool exclude_gaps, const char *ref_seq_name) 
{
	IntVector kept_sites;
	buildRetainingSites(aln_site_list, kept_sites, exclude_gaps, ref_seq_name);
	try {
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);
		if (append)
			out.open(file_name, ios_base::out | ios_base::app);
		else
			out.open(file_name);
		StrVector::iterator it;
		int seq_id = 0;
		for (it = seq_names.begin(); it != seq_names.end(); it++, seq_id++) {
			out << ">" << (*it) << endl;
			int j = 0;
			for (IntVector::iterator i = site_pattern.begin();  i != site_pattern.end(); i++, j++) 
				if (kept_sites[j])
					out << convertStateBack(at(*i)[seq_id]);
			out << endl;
		}
		out.close();
		cout << "Alignment was printed to " << file_name << endl;
	} catch (ios::failure) {
		outError(ERR_WRITE_OUTPUT, file_name);
	}	
}


void Alignment::extractSubAlignment(Alignment *aln, IntVector &seq_id, int min_true_char) {
	IntVector::iterator it;
	for (it = seq_id.begin(); it != seq_id.end(); it++) {
		assert(*it >= 0 && *it < aln->getNSeq());
		seq_names.push_back(aln->getSeqName(*it));
	}
	num_states = aln->num_states;
	site_pattern.resize(aln->getNSite(), -1);
	clear();
	pattern_index.clear();
	int site = 0;
	VerboseMode save_mode = verbose_mode; 
	verbose_mode = VB_MIN; // to avoid printing gappy sites in addPattern
	for (iterator pit = aln->begin(); pit != aln->end(); pit++) {
		Pattern pat;
		int true_char = 0;
		for (it = seq_id.begin(); it != seq_id.end(); it++) {
			char ch = (*pit)[*it];
			if (ch != STATE_UNKNOWN) true_char++;
			pat.push_back(ch);
		}
		if (true_char < min_true_char) continue;
		addPattern(pat, site, (*pit).frequency);
		for (int i = 0; i < (*pit).frequency; i++)
			site_pattern[site++] = size()-1;
	}
	site_pattern.resize(site);
	verbose_mode = save_mode;
	countConstSite();
	assert(size() <= aln->size());
}


void Alignment::extractPatterns(Alignment *aln, IntVector &ptn_id) {
	int i;
	for (i = 0; i < aln->getNSeq(); i++) {
		seq_names.push_back(aln->getSeqName(i));
	}
	num_states = aln->num_states;
	site_pattern.resize(aln->getNSite(), -1);
	clear();
	pattern_index.clear();
	int site = 0;
	VerboseMode save_mode = verbose_mode; 
	verbose_mode = VB_MIN; // to avoid printing gappy sites in addPattern
	for (i = 0; i != ptn_id.size(); i++) {
		assert(ptn_id[i] >= 0 && ptn_id[i] < aln->getNPattern());
		Pattern pat = aln->at(ptn_id[i]);
		addPattern(pat, site, aln->at(ptn_id[i]).frequency);
		for (int j = 0; j < aln->at(ptn_id[i]).frequency; j++)
			site_pattern[site++] = size()-1;
	}
	site_pattern.resize(site);
	verbose_mode = save_mode;
	countConstSite();
	assert(size() <= aln->size());
}

void Alignment::createBootstrapAlignment(Alignment *aln) {
	int site, nsite = aln->getNSite();
	seq_names.insert(seq_names.begin(), aln->seq_names.begin(), aln->seq_names.end());
	num_states = aln->num_states;
	site_pattern.resize(nsite, -1);
	clear();
	pattern_index.clear();
	VerboseMode save_mode = verbose_mode; 
	verbose_mode = VB_MIN; // to avoid printing gappy sites in addPattern
	for (site = 0; site < nsite; site++) {
		int ptn_id = aln->getPatternID(floor((((double)rand())/RAND_MAX) * nsite));
 		Pattern pat = aln->at(ptn_id);
		addPattern(pat, site);
	}
	verbose_mode = save_mode;
	countConstSite();
}


void Alignment::countConstSite() {
	int num_const_sites = 0;
	for (iterator it = begin(); it != end(); it++)
		if ((*it).is_const) num_const_sites += (*it).frequency;
	frac_const_sites = ((double)num_const_sites) / getNSite();
}

int Alignment::countProperChar(int seq_id) {
	int num_proper_chars = 0;
	for (iterator it = begin(); it != end(); it++) {
		if ((*it)[seq_id] >= 0 && (*it)[seq_id] < num_states) num_proper_chars+=(*it).frequency;
	}
	return num_proper_chars;
}

Alignment::~Alignment()
{
}

double Alignment::computeObsDist(int seq1, int seq2) {
	int diff_pos = 0, total_pos = 0;
	for (iterator it = begin(); it != end(); it++) 
		if  ((*it)[seq1] < num_states && (*it)[seq2] < num_states) {
		//if ((*it)[seq1] != STATE_UNKNOWN && (*it)[seq2] != STATE_UNKNOWN) {
			total_pos += (*it).frequency;
			if ((*it)[seq1] != (*it)[seq2] )
				diff_pos += (*it).frequency;
		}
	if (!total_pos) total_pos = 1;
	return ((double)diff_pos) / total_pos;
}

double Alignment::computeJCDist(int seq1, int seq2) {
	double obs_dist = computeObsDist(seq1, seq2);
	double z = (double)num_states / (num_states-1);
	double x = 1.0 - (z * obs_dist);
	if (x <= 0) {
		string str = "Too long distance between two sequences ";
		str += getSeqName(seq1);
		str += " and ";
		str += getSeqName(seq2);
		outWarning(str);
		return MAX_GENETIC_DIST;
	}

	return -log(x) / z;
}

void Alignment::printDist(ostream &out, double *dist_mat) {
	int nseqs = getNSeq();
	int max_len = getMaxSeqNameLength();
	if (max_len < 10) max_len = 10;
	out << nseqs << endl;
	int pos = 0;
	out.precision(6);
	out << fixed;
	for (int seq1 = 0; seq1 < nseqs; seq1 ++)  {
		out.width(max_len);
		out << left << getSeqName(seq1) << " ";
		for (int seq2 = 0; seq2 < nseqs; seq2 ++) {
			out << dist_mat[pos++];
			/*if (seq2 % 7 == 6) {
				out << endl;
				out.width(max_len+1);
			} */
			out << " "; 
		}	
		out << endl;
	}
}

void Alignment::printDist(const char *file_name, double *dist_mat) {
	try {
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);
		out.open(file_name);
		printDist(out, dist_mat);
		out.close();
		//cout << "Distance matrix was printed to " << file_name << endl;
	} catch (ios::failure) {
		outError(ERR_WRITE_OUTPUT, file_name);
	}	
}

void Alignment::readDist(istream &in, double *dist_mat) {
	int nseqs;
	in >> nseqs;
	if (nseqs != getNSeq())
		throw "Distance file has different number of taxa";
	int pos = 0, seq1, seq2;
	for (seq1 = 0; seq1 < nseqs; seq1 ++)  {
		string seq_name;
		in >> seq_name;
		if (seq_name != getSeqName(seq1))
			throw "Sequence name " + seq_name + " is different from " + getSeqName(seq1);
		for (seq2 = 0; seq2 < nseqs; seq2 ++) {
			in >> dist_mat[pos++];
		}	
	}
	// check for symmetric matrix
	for (seq1 = 0; seq1 < nseqs-1; seq1++) {
		if (dist_mat[seq1*nseqs+seq1] != 0.0)
			throw "Diagonal elements of distance matrix is not ZERO";
		for (seq2 = seq1+1; seq2 < nseqs; seq2++)
			if (dist_mat[seq1*nseqs+seq2] != dist_mat[seq2*nseqs+seq1])
				throw "Distance between " + getSeqName(seq1) + " and " + getSeqName(seq2) + " is not symmetric";
	}
}

void Alignment::readDist(const char *file_name, double *dist_mat) {
	try {
		ifstream in;
		in.exceptions(ios::failbit | ios::badbit);
		in.open(file_name);
		readDist(in, dist_mat);
		in.close();
		cout << "Distance matrix was read from " << file_name << endl;
	} catch (const char *str) {
		outError(str);
	} catch (string str) {
		outError(str);
	} catch (ios::failure) {
		outError(ERR_READ_INPUT, file_name);
	}
	
}


void Alignment::computeStateFreq (double *stateFrqArr) {
	int stateNo_;
	int nState_ = num_states;
	int nseqs = getNSeq();
	double timeAppArr_[num_states];
	double siteAppArr_[num_states]; //App = appearance
	double newSiteAppArr_[num_states];

	for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++)
		stateFrqArr [ stateNo_ ] = 1.0 / nState_;

	int NUM_TIME = 8;
	//app = appeareance
	for (int time_ = 0; time_ < NUM_TIME; time_ ++) 
	{
		for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++)
			timeAppArr_[stateNo_] = 0.0;

		for (iterator it = begin(); it != end(); it++) 
			for (int i = 0; i < (*it).frequency; i++)	
			{
			for (int seq = 0; seq < nseqs; seq++) {
				int stateNo_ = (*it)[seq];

				getAppearance (stateNo_, siteAppArr_);

				double totalSiteApp_ = 0.0;
				for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++) {
					newSiteAppArr_[stateNo_] = stateFrqArr[stateNo_] * siteAppArr_[stateNo_];
					totalSiteApp_ += newSiteAppArr_[stateNo_];
				}

				for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++)
					timeAppArr_[stateNo_] += newSiteAppArr_[stateNo_] / totalSiteApp_;
			}
		}

		double totalTimeApp_ = 0.0;
		int stateNo_;
		for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++)
			totalTimeApp_ += timeAppArr_[stateNo_];


		for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++)
			stateFrqArr[stateNo_] = timeAppArr_[stateNo_] / totalTimeApp_;

	} //end of for time_

	//  std::cout << "state frequency ..." << endl;
	// for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++)
	// std::cout << stateFrqArr[stateNo_] << endl;

	if (verbose_mode >= VB_DEBUG) {
		cout << "Empirical state frequencies: ";
		for (stateNo_ = 0; stateNo_ < nState_; stateNo_ ++)
			cout << stateFrqArr[stateNo_] << " ";
		cout << endl;
	}

}

void Alignment::getAppearance(char state, double *state_app) {
	int i;
	if (state == STATE_UNKNOWN) {
		for (i = 0; i < num_states; i++)
			state_app[i] = 1.0;
		return;
	}

	memset(state_app, 0, num_states * sizeof(double));
	if (state < num_states) {
		state_app[(int)state] = 1.0;
		return;
	}
	state -= (num_states-1);
	for (i = 0; i < num_states; i++) 
	if (state & (1 << i)) {
		state_app[i] = 1.0;
	}
}


void Alignment::computeEmpiricalRate (double *rates) {
	int i, j, k;
	assert(rates);
	int nseqs = getNSeq();
	double **pair_rates = (double**) new double[num_states];
	for (i = 0; i < num_states; i++) {
		pair_rates[i] = new double[num_states];
		memset(pair_rates[i], 0, sizeof(double)*num_states);
	}

	for (iterator it = begin(); it != end(); it++) {
		for (i = 0; i < nseqs-1; i++) {
			char state1 = (*it)[i];
			if (state1 >= num_states) continue;
			for (j = i+1; j < nseqs; j++) {
				char state2 = (*it)[j];
				if (state2 < num_states) pair_rates[(int)state1][(int)state2] += (*it).frequency;
			}
		}
	}

	k = 0;
	double last_rate = pair_rates[num_states-2][num_states-1] + pair_rates[num_states-1][num_states-2];
	if (last_rate == 0) last_rate = 1;
	for (i = 0; i < num_states-1; i++)
		for (j = i+1; j < num_states; j++) {
			rates[k++] = (pair_rates[i][j] + pair_rates[j][i]) / last_rate;
			if (rates[k-1] == 0) rates[k-1] = 1e-4;
		}
	rates[k-1] = 1;
	if (verbose_mode >= VB_DEBUG) {
		cout << "Empirical rates: ";
		for (k = 0; k < num_states*(num_states-1)/2; k++)
			cout << rates[k] << " ";
		cout << endl;
	}

	for (i = num_states-1; i >= 0; i--) {
		delete [] pair_rates[i];
	}
	delete [] pair_rates;
}

void Alignment::computeEmpiricalRateNonRev (double *rates) {
	double rates_mat[num_states*num_states];
	int i, j, k;

	computeEmpiricalRate(rates);
	
	for (i = 0, k = 0; i < num_states-1; i++)
		for (j = i+1; j < num_states; j++)
			rates_mat[i*num_states+j] = rates_mat[j*num_states+i] = rates[k++];
			
	for (i = 0, k = 0; i < num_states; i++)
		for (j = 0; j < num_states; j++)
			if (j != i) rates[k++] = rates_mat[i*num_states+j];
	
}

double Alignment::computeUnconstrainedLogL() {
	int nptn = size();
	double logl = 0.0;
	int nsite = getNSite(), i;
	double lognsite = log(nsite);
	for (i = 0; i < nptn; i++)
		logl += (log(at(i).frequency) - lognsite) * at(i).frequency;
	return logl;
}

