#include <iostream>
#include <fstream>
#include <string>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <pthread.h>
#include <map>
#include "selscan-data.h"
#include "selscan-pbar.h"
#include "binom.h"
#include "param_t.h"

using namespace std;

const string ARG_THREAD = "--threads";
const int DEFAULT_THREAD = 1;
const string HELP_THREAD = "The number of threads to spawn during the calculation.  Partitions locus calculations across threads.";

const string ARG_FILENAME_POP1 = "--hap";
const string DEFAULT_FILENAME_POP1 = "__hapfile1";
const string HELP_FILENAME_POP1 = "A hapfile with one row per individual, and one column per variant.  Variants should be coded 0/1/-9.";

const string ARG_FILENAME_POP2 = "--ref";
const string DEFAULT_FILENAME_POP2 = "__hapfile2";
const string HELP_FILENAME_POP2 = "A hapfile with one row per individual, and one column per variant.  Variants should be coded 0/1/-9. This is the reference population for XP-EHH calculations.  Ignored otherwise.";

const string ARG_FILENAME_MAP = "--map";
const string DEFAULT_FILENAME_MAP = "__mapfile";
const string HELP_FILENAME_MAP = "A mapfile with one row per variant site.  Formatted <chr#> <locusID> <genetic pos> <physical pos>.";

const string ARG_OUTFILE = "--out";
const string DEFAULT_OUTFILE = "outfile";
const string HELP_OUTFILE = "The basename for all output files.";

const string ARG_CUTOFF = "--cutoff";
const double DEFAULT_CUTOFF = 0.05;
const string HELP_CUTOFF = "The EHH decay cutoff.";

const string ARG_MAX_GAP = "--max-gap";
const int DEFAULT_MAX_GAP = 200000;
const string HELP_MAX_GAP = "Maximum allowed gap between two snps.";

const string ARG_GAP_SCALE = "--gap-scale";
const int DEFAULT_GAP_SCALE = 20000;
const string HELP_GAP_SCALE = "Gap scale parameter.  If a gap is encountered between two snps > GAP_SCALE and < MAX_GAP, then the genetic distance is scaled by GAP_SCALE/GAP.";

const string ARG_IHS = "--ihs";
const bool DEFAULT_IHS = false;
const string HELP_IHS = "Set this flag to calculate iHS.";

const string ARG_SOFT = "--soft";
const bool DEFAULT_SOFT = false;
const string HELP_SOFT = "Set this flag to calculate the soft sweep statistics.";

const string ARG_XP = "--xpehh";
const bool DEFAULT_XP = false;
const string HELP_XP = "Set this flag to calculate XP-EHH.";

const string ARG_EXACT = "--alt";
const bool DEFAULT_EXACT = false;
const string HELP_EXACT = "If --ihs is set, this flag will calculate the exact homozygosity of the sample instead of using allele frequencies.\n\tIf --sumsq or --sqsum or --sumfreq is set, this flag will change ratio from H_c/H to H_c/(H_r + 1).";

const string ARG_FREQ = "--filter";
const double DEFAULT_FREQ = 0.05;
const string HELP_FREQ = "If a site has a MAF below this value, the program will not use it as a core snp.";

const string ARG_QUERY = "--query";
const string DEFAULT_QUERY = "__NO_LOCUS__";
const string HELP_QUERY = "Query a specific locus instead of scanning the whole dataset.";

const string ARG_QWIN = "--qwin";
const int DEFAULT_QWIN = 100000;
const string HELP_QWIN = "The length of the query window in each direction from the query locus.";


pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;

struct work_order_t
{
    int first_index;
    int last_index;
    int queryLoc;

    string filename;

    HaplotypeData *hapData;

    HaplotypeData *hapData1;
    HaplotypeData *hapData2;

    MapData *mapData;

    double (*calc)(map<string, int> &, int, bool);

    double *ihs;
    double *freq;

    double *ihh1;
    double *freq1;

    double *ihh2;
    double *freq2;

    bool *pass;

    ofstream *flog;
    ofstream *fout;
    Bar *bar;

    param_t *params;
};

struct triplet_t
{
    double h1;
    double h12;
    double h2dh1;
};

triplet_t calculateSoft(map<string, int> &count, int total);

void query_locus(void *work_order);
void query_locus_soft(void *order);

void calc_ihs(void *work_order);
void calc_xpihh(void *work_order);
void calc_soft_ihs(void *order);

bool *freqMask(HaplotypeData *hapData, double filter);
double calcFreq(HaplotypeData *hapData, int locus);
int queryFound(MapData *mapData, string query);
void fillColors(int **hapColor, map<string, int> &hapCount,
                string *haplotypeList, int hapListLength,
                int currentLoc, int &currentColor, bool left);
bool familyDidSplit(const string &hapStr, const int hapCount,
                    int **hapColor, const int nhaps, const int colorIndex,
                    const int previousLoc, string &mostCommonHap);

double calculateHomozygosity(map<string, int> &count, int total, bool EXACT);

int main(int argc, char *argv[])
{
    param_t params;
    params.addFlag(ARG_THREAD, DEFAULT_THREAD, "", HELP_THREAD);
    params.addFlag(ARG_FILENAME_POP1, DEFAULT_FILENAME_POP1, "", HELP_FILENAME_POP1);
    params.addFlag(ARG_FILENAME_POP2, DEFAULT_FILENAME_POP2, "", HELP_FILENAME_POP2);
    params.addFlag(ARG_FILENAME_MAP, DEFAULT_FILENAME_MAP, "", HELP_FILENAME_MAP);
    params.addFlag(ARG_OUTFILE, DEFAULT_OUTFILE, "", HELP_OUTFILE);
    params.addFlag(ARG_CUTOFF, DEFAULT_CUTOFF, "", HELP_CUTOFF);
    params.addFlag(ARG_MAX_GAP, DEFAULT_MAX_GAP, "", HELP_MAX_GAP);
    params.addFlag(ARG_GAP_SCALE, DEFAULT_GAP_SCALE, "", HELP_GAP_SCALE);
    params.addFlag(ARG_IHS, DEFAULT_IHS, "", HELP_IHS);
    params.addFlag(ARG_SOFT, DEFAULT_SOFT, "", HELP_SOFT);
    params.addFlag(ARG_XP, DEFAULT_XP, "", HELP_XP);
    params.addFlag(ARG_EXACT, DEFAULT_EXACT, "", HELP_EXACT);
    params.addFlag(ARG_FREQ, DEFAULT_FREQ, "", HELP_FREQ);
    params.addFlag(ARG_QUERY, DEFAULT_QUERY, "", HELP_QUERY);
    params.addFlag(ARG_QWIN, DEFAULT_QWIN, "", HELP_QWIN);

    try
    {
        params.parseCommandLine(argc, argv);
    }
    catch (...)
    {
        return 1;
    }

    string hapFilename = params.getStringFlag(ARG_FILENAME_POP1);
    string hapFilename2 = params.getStringFlag(ARG_FILENAME_POP2);
    string mapFilename = params.getStringFlag(ARG_FILENAME_MAP);
    string outFilename = params.getStringFlag(ARG_OUTFILE);
    string query = params.getStringFlag(ARG_QUERY);

    int queryLoc = -1;
    int numThreads = params.getIntFlag(ARG_THREAD);
    int SCALE_PARAMETER = params.getIntFlag(ARG_GAP_SCALE);
    int MAX_GAP = params.getIntFlag(ARG_MAX_GAP);

    double EHH_CUTOFF = params.getDoubleFlag(ARG_CUTOFF);
    double FILTER = params.getDoubleFlag(ARG_FREQ);

    bool EXACT = params.getBoolFlag(ARG_EXACT);
    bool CALC_IHS = params.getBoolFlag(ARG_IHS);
    bool CALC_XP = params.getBoolFlag(ARG_XP);
    bool CALC_SOFT = params.getBoolFlag(ARG_SOFT);
    bool SINGLE_QUERY = false;

    if (query.compare(DEFAULT_QUERY) != 0) SINGLE_QUERY = true;


    if (CALC_IHS + CALC_XP + CALC_SOFT != 1)
    {
        cerr << "ERROR: Must specify one and only one of iHS (" << ARG_IHS << "), XP-EHH (" << ARG_XP << "), soft iHS (" << ARG_SOFT << ")\n";
        return 1;
    }

    if (SINGLE_QUERY && CALC_XP)
    {
        cerr << "Single query with XP-EHH is not yet available.\n";
        return 1;
    }


    if (SINGLE_QUERY) outFilename += "." + query;

    if (CALC_IHS) outFilename += ".ihs";
    else if (CALC_XP) outFilename += ".xpehh";
    else if (CALC_SOFT) outFilename += ".soft";

    if (EXACT) outFilename += ".alt";

    if (numThreads < 1)
    {
        cerr << "ERROR: Must run with one or more threads.\n";
        return 1;
    }
    if (SCALE_PARAMETER < 1)
    {
        cerr << "ERROR: Scale parameter must be positive.";
        return 1;
    }
    if (MAX_GAP < 1)
    {
        cerr << "ERROR: Max gap parameter must be positive.";
        return 1;
    }
    if (EHH_CUTOFF <= 0 || EHH_CUTOFF >= 1)
    {
        cerr << "ERROR: EHH cut off must be > 0 and < 1.";
        return 1;
    }

    if ((CALC_IHS) && hapFilename2.compare(DEFAULT_FILENAME_POP2) != 0)
    {
        cerr << "ERROR: You are calculating iHS for " << hapFilename << ", but have also given a second data file (" << hapFilename2 << ").\n";
        return 1;
    }

    HaplotypeData *hapData, *hapData2;
    MapData *mapData;

    try
    {
        hapData = readHaplotypeData(hapFilename);
        if (CALC_XP)
        {
            hapData2 = readHaplotypeData(hapFilename2);
            if (hapData->nloci != hapData2->nloci)
            {
                cerr << "ERROR: Haplotypes from " << hapFilename << " and " << hapFilename2 << " do not have the same number of loci.\n";
                return 1;
            }
        }
        mapData = readMapData(mapFilename, hapData->nloci);
    }
    catch (...)
    {
        return 1;
    }

    if (SINGLE_QUERY)
    {
        queryLoc = queryFound(mapData, query);
        if (queryLoc < 0)
        {
            cerr << "ERROR: Could not find specific locus query, " << query << ", in data.\n";
            return 1;
        }
        else
        {
            cerr << "Found " << query << " at line " << queryLoc + 1 << ".\n";
        }
    }

    //Open stream for log file
    ofstream flog;
    string logFilename = outFilename + ".log";
    flog.open(logFilename.c_str());
    if (flog.fail())
    {
        cerr << "ERROR: could not open " << logFilename << " for writing.\n";
        return 1;
    }

    //Open stream for output file
    ofstream fout;
    outFilename += ".out";
    fout.open(outFilename.c_str());
    if (fout.fail())
    {
        cerr << "ERROR: could not open " << outFilename << " for writing.\n";
        return 1;
    }


    for (int i = 0; i < argc; i++)
    {
        flog << argv[i] << " ";
    }
    flog << "\n\nCalculating ";
    if (CALC_XP) flog << "XP-EHH.\n";
    else flog << " iHS.\n";
    flog << "Haplotypes filename: " << hapFilename << "\n";
    if (CALC_XP) flog << "Reference haplotypes filename: " << hapFilename2 << "\n";
    flog << "Map filename: " << mapFilename << "\n";
    flog << "Output file: " << outFilename << "\n";
    flog << "Threads: " << numThreads << "\n";
    flog << "Scale parameter: " << SCALE_PARAMETER << "\n";
    flog << "Max gap parameter: " << MAX_GAP << "\n";
    flog << "EHH cutoff value: " << EHH_CUTOFF << "\n";
    flog << "Alt flag set: ";
    if (EXACT) flog << "yes\n";
    else flog << "no\n";
    flog.flush();

    Bar pbar;
    barInit(pbar, mapData->nloci, 78);

    double *ihs, *ihh1, *ihh2;
    double *freq, *freq1, *freq2;

    if (mapData->nloci < numThreads)
    {
        numThreads = mapData->nloci;
        cerr << "WARNING: there are fewer loci than threads requested.  Running with " << numThreads << " threads instead.\n";
    }

    //Partition loci amongst the specified threads
    unsigned long int *NUM_PER_THREAD = new unsigned long int[numThreads];
    unsigned long int div = mapData->nloci / numThreads;

    for (int i = 0; i < numThreads; i++)
    {
        NUM_PER_THREAD[i] = 0;
        NUM_PER_THREAD[i] += div;
    }

    for (int i = 0; i < (mapData->nloci) % numThreads; i++)
    {
        NUM_PER_THREAD[i]++;
    }

    if (SINGLE_QUERY)
    {
        work_order_t *order = new work_order_t;
        pthread_t *peer = new pthread_t;
        order->hapData = hapData;
        order->mapData = mapData;
        order->flog = &flog;
        order->fout = &fout;
        order->filename = outFilename;
        order->params = &params;
        order->queryLoc = queryLoc;
        order->calc = &calculateHomozygosity;

        if (CALC_SOFT)
        {
            pthread_create(peer,
                           NULL,
                           (void *(*)(void *))query_locus_soft,
                           (void *)order);
            pthread_join(*peer, NULL);
        }
        else if (CALC_IHS)
        {
            pthread_create(peer,
                           NULL,
                           (void *(*)(void *))query_locus,
                           (void *)order);

            ofstream fout2;
            outFilename += ".score.out";
            fout2.open(outFilename.c_str());
            if (fout2.fail())
            {
                cerr << "ERROR: could not open " << outFilename << " for writing.\n";
                return 1;
            }

            work_order_t *order2 = new work_order_t;
            pthread_t *peer2 = new pthread_t;
            order = new work_order_t;

            //A horrible hack, because we don't need to allcoate anywhere near this amount of memory for a single calculation
            //But otherwise it requires serious modification of the calc_ihs function to get it to run properly
            ihh1 = new double[mapData->nloci];
            ihh2 = new double[mapData->nloci];
            ihs = new double[mapData->nloci];
            freq = new double[mapData->nloci];

            order2->first_index = queryLoc;
            order2->last_index = queryLoc + 1;
            order2->hapData = hapData;
            order2->mapData = mapData;
            order2->ihh1 = ihh1;
            order2->ihh2 = ihh2;
            order2->ihs = ihs;
            order2->freq = freq;
            order2->flog = &flog;
            order2->bar = &pbar;
            order2->params = &params;
            order2->calc = &calculateHomozygosity;

            pthread_create(peer2,
                           NULL,
                           (void *(*)(void *))calc_ihs,
                           (void *)order2);

            pthread_join(*peer2, NULL);
            pthread_join(*peer, NULL);

            if (ihs[queryLoc] != MISSING)
            {
                fout2 << mapData->locusName[queryLoc] << "\t"
                      << mapData->physicalPos[queryLoc] << "\t"
                      << freq[queryLoc] << "\t"
                      << ihh1[queryLoc] << "\t"
                      << ihh2[queryLoc] << "\t"
                      << ihs[queryLoc] << endl;
            }

            delete peer2;
            delete order2;
        }

        delete peer;
        delete order;
        return 0;
    }

    ihh1 = new double[mapData->nloci];
    ihh2 = new double[mapData->nloci];

    if (CALC_XP)
    {
        freq1 = new double[hapData->nloci];
        freq2 = new double[hapData2->nloci];

        cerr << "Starting XP-EHH calculations.\n";
        work_order_t *order;
        pthread_t *peer = new pthread_t[numThreads];
        int prev_index = 0;
        for (int i = 0; i < numThreads; i++)
        {
            order = new work_order_t;
            order->first_index = prev_index;
            order->last_index = prev_index + NUM_PER_THREAD[i];
            prev_index += NUM_PER_THREAD[i];
            order->hapData1 = hapData;
            order->hapData2 = hapData2;
            order->mapData = mapData;
            order->ihh1 = ihh1;
            order->ihh2 = ihh2;
            order->freq1 = freq1;
            order->freq2 = freq2;
            order->flog = &flog;
            order->bar = &pbar;
            order->params = &params;
            pthread_create(&(peer[i]),
                           NULL,
                           (void *(*)(void *))calc_xpihh,
                           (void *)order);
        }

        for (int i = 0; i < numThreads; i++)
        {
            pthread_join(peer[i], NULL);
        }

        delete [] peer;
        releaseHapData(hapData);
        releaseHapData(hapData2);
        cerr << "\nFinished.\n";

        fout << "pos gpos p1 ihh1 p2 ihh2 xpehh\n";
        for (int i = 0; i < mapData->nloci; i++)
        {
            if (ihh1[i] != MISSING && ihh2[i] != MISSING && ihh1[i] != 0 && ihh2[i] != 0)
            {
                fout << mapData->locusName[i] << "\t"
                     << mapData->physicalPos[i] << "\t"
                     << mapData->geneticPos[i] << "\t"
                     << freq1[i] << "\t"
                     << ihh1[i] << "\t"
                     << freq2[i] << "\t"
                     << ihh2[i] << "\t";
                fout << log(ihh1[i] / ihh2[i]) << endl;
            }
        }
    }
    else if (CALC_IHS)
    {
        ihs = new double[hapData->nloci];
        freq = new double[hapData->nloci];
        cerr << "Starting iHS calculations with alt flag ";
        if (!EXACT) cerr << "not ";
        cerr << "set.\n";

        work_order_t *order;
        pthread_t *peer = new pthread_t[numThreads];
        int prev_index = 0;
        for (int i = 0; i < numThreads; i++)
        {
            order = new work_order_t;
            order->first_index = prev_index;
            order->last_index = prev_index + NUM_PER_THREAD[i];
            prev_index += NUM_PER_THREAD[i];
            order->hapData = hapData;
            order->mapData = mapData;
            order->ihh1 = ihh1;
            order->ihh2 = ihh2;
            order->ihs = ihs;
            order->freq = freq;
            order->flog = &flog;
            order->bar = &pbar;
            order->params = &params;
            order->calc = &calculateHomozygosity;

            pthread_create(&(peer[i]),
                           NULL,
                           (void *(*)(void *))calc_ihs,
                           (void *)order);
        }

        for (int i = 0; i < numThreads; i++)
        {
            pthread_join(peer[i], NULL);
        }

        delete [] peer;
        releaseHapData(hapData);
        cerr << "\nFinished.\n";

        for (int i = 0; i < mapData->nloci; i++)
        {
            if (ihs[i] != MISSING && ihh1[i] != 0 && ihh2[i] != 0)
            {
                fout << mapData->locusName[i] << "\t"
                     << mapData->physicalPos[i] << "\t"
                     << freq[i] << "\t"
                     << ihh1[i] << "\t"
                     << ihh2[i] << "\t"
                     << ihs[i] << endl;
            }
        }
    }
    else if (CALC_SOFT)
    {
        ihs = new double[hapData->nloci];
        freq = new double[hapData->nloci];
        cerr << "Starting soft iHS calculations with alt flag ";
        if (!EXACT) cerr << "not ";
        cerr << "set.\n";

        work_order_t *order;
        pthread_t *peer = new pthread_t[numThreads];
        int prev_index = 0;
        for (int i = 0; i < numThreads; i++)
        {
            order = new work_order_t;
            order->first_index = prev_index;
            order->last_index = prev_index + NUM_PER_THREAD[i];
            prev_index += NUM_PER_THREAD[i];
            order->hapData = hapData;
            order->mapData = mapData;
            order->ihh1 = ihh1;
            order->ihh2 = ihh2;
            order->ihs = ihs;
            order->freq = freq;
            order->flog = &flog;
            order->bar = &pbar;
            order->params = &params;

            pthread_create(&(peer[i]),
                           NULL,
                           (void *(*)(void *))calc_soft_ihs,
                           (void *)order);
        }

        for (int i = 0; i < numThreads; i++)
        {
            pthread_join(peer[i], NULL);
        }

        delete [] peer;
        releaseHapData(hapData);
        cerr << "\nFinished.\n";

        for (int i = 0; i < mapData->nloci; i++)
        {
            if (ihs[i] != MISSING && ihh1[i] != MISSING && ihh2[i] != MISSING)
            {
                fout << mapData->locusName[i] << "\t";
                fout << mapData->physicalPos[i] << "\t";
                fout << freq[i] << "\t";
                fout << ihh1[i] << "\t"; //ihh1
                fout << ihs[i] << "\t"; //ihh12
                fout << ihh2[i] << endl; //ihh2d1
            }
        }
    }

    flog.close();
    fout.close();
    return 0;
}

int queryFound(MapData *mapData, string query)
{
    for (int locus = 0; locus < mapData->nloci; locus++)
    {
        if (mapData->locusName[locus].compare(query) == 0) return locus;
    }

    return -1;
}

double calcFreq(HaplotypeData *hapData, int locus)
{
    double total = 0;
    double freq = 0;

    for (int hap = 0; hap < hapData->nhaps; hap++)
    {
        if (hapData->data[hap][locus] != -9)
        {
            freq += hapData->data[hap][locus];
            total++;
        }
    }
    return (freq / total);
}

void query_locus(void *order)
{
    work_order_t *p = (work_order_t *)order;
    short **data = p->hapData->data;
    int nloci = p->hapData->nloci;
    int nhaps = p->hapData->nhaps;
    int *physicalPos = p->mapData->physicalPos;
    double *geneticPos = p->mapData->geneticPos;
    string *locusName = p->mapData->locusName;
    ofstream *flog = p->flog;
    ofstream *fout = p->fout;
    string outFilename = p->filename;
    int SCALE_PARAMETER = p->params->getIntFlag(ARG_GAP_SCALE);
    int MAX_GAP = p->params->getIntFlag(ARG_MAX_GAP);
    double EHH_CUTOFF = p->params->getDoubleFlag(ARG_CUTOFF);
    bool EXACT = p->params->getBoolFlag(ARG_EXACT);
    double (*calc)(map<string, int> &, int, bool) = p->calc;

    int locus = p->queryLoc;
    int queryPad = p->params->getIntFlag(ARG_QWIN);
    int stopLeft = locus;
    for (int i = locus - 1; i >= 0; i--)
    {
        if (physicalPos[locus] - physicalPos[i] <= queryPad) stopLeft = i;
    }
    int stopRight = locus;
    for (int i = locus + 1; i < nloci; i++)
    {
        if (physicalPos[i] - physicalPos[locus] <= queryPad) stopRight = i;
    }

    //EHH to the left of the core snp
    double current_derived_ehh = 1;
    double current_ancestral_ehh = 1;
    double derivedCount = 0;

    //A list of all the haplotypes
    //Starts with just the focal snp and grows outward
    string *haplotypeList = new string[nhaps];
    for (int hap = 0; hap < nhaps; hap++)
    {
        derivedCount += data[hap][locus];
        char digit[2];
        sprintf(digit, "%d", data[hap][locus]);
        haplotypeList[hap] = digit;
    }

    if (derivedCount == 0 || derivedCount == nhaps)
    {
        cerr << "ERROR: " << locusName[locus] << " is monomorphic.\n";
        (*fout) << "ERROR: " << locusName[locus] << " is monomorphic.\n";
        return;
    }

    //cerr << "numHaps: " << nhaps << "\nderivedCounts: " << derivedCount << endl;

    int **ancestralHapColor = new int *[int(nhaps - derivedCount)];
    for (int i = 0; i < nhaps - derivedCount; i++)
    {
        ancestralHapColor[i] = new int[stopRight - stopLeft + 1];
        ancestralHapColor[i][locus - stopLeft] = 0;
    }
    int **derivedHapColor = new int *[int(derivedCount)];
    for (int i = 0; i < derivedCount; i++)
    {
        derivedHapColor[i] = new int[stopRight - stopLeft + 1];
        derivedHapColor[i][locus - stopLeft] = 0;
    }

    //cerr << "allocated hap color arrays.\n";

    string *tempResults = new string[locus - stopLeft];
    int tempIndex = locus - stopLeft - 1;
    int derivedCurrentColor = 0;
    int ancestralCurrentColor = 0;

    for (int i = locus - 1; i >= stopLeft; i--)
    {
        int numDerived = 0;
        int numAncestral = 0;
        map<string, int> ancestralHapCount;
        map<string, int> derivedHapCount;

        for (int hap = 0; hap < nhaps; hap++)
        {
            bool isDerived = data[hap][locus];
            //build haplotype string
            char digit[2];
            sprintf(digit, "%d", data[hap][i]);
            haplotypeList[hap] += digit;
            string hapStr = haplotypeList[hap];

            if (isDerived)
            {
                //count derived hapoltype
                if (derivedHapCount.count(hapStr) == 0) derivedHapCount[hapStr] = 1;
                else derivedHapCount[hapStr]++;
                numDerived++;
            }
            else
            {
                //count ancestral haplotype
                if (ancestralHapCount.count(hapStr) == 0) ancestralHapCount[hapStr] = 1;
                else ancestralHapCount[hapStr]++;
                numAncestral++;
            }
        }
        //Write functions to fill in haplotype colors here
        fillColors(derivedHapColor, derivedHapCount, haplotypeList, nhaps, tempIndex, derivedCurrentColor, true);
        fillColors(ancestralHapColor, ancestralHapCount, haplotypeList, nhaps, tempIndex, ancestralCurrentColor, true);

        current_derived_ehh = (*calc)(derivedHapCount, numDerived, EXACT);
        current_ancestral_ehh = (*calc)(ancestralHapCount, numAncestral, EXACT);
        char tempStr[100];
        sprintf(tempStr, "%d\t%f\t%f", physicalPos[i] - physicalPos[locus], current_derived_ehh, current_ancestral_ehh);
        tempResults[tempIndex] = string(tempStr);
        tempIndex--;
    }

    delete [] haplotypeList;

    for (int i = 0; i < locus - stopLeft; i++)
    {
        (*fout) << tempResults[i] << "\n";
    }
    delete [] tempResults;

    //calculate EHH to the right
    current_derived_ehh = 1;
    current_ancestral_ehh = 1;

    fout->precision(6);
    (*fout) << std::fixed <<  physicalPos[locus] - physicalPos[locus]  << "\t"
            << current_derived_ehh << "\t"
            << current_ancestral_ehh << endl;

    //A list of all the haplotypes
    //Starts with just the focal snp and grows outward
    haplotypeList = new string[nhaps];
    for (int hap = 0; hap < nhaps; hap++)
    {
        char digit[2];
        sprintf(digit, "%d", data[hap][locus]);
        haplotypeList[hap] = digit;
    }

    derivedCurrentColor = 0;
    ancestralCurrentColor = 0;

    //while(current_ancestral_ehh > EHH_CUTOFF || current_derived_ehh > EHH_CUTOFF)
    for (int i = locus + 1; i <= stopRight; i++)
    {
        int numDerived = 0;
        int numAncestral = 0;
        map<string, int> ancestralHapCount;
        map<string, int> derivedHapCount;
        for (int hap = 0; hap < nhaps; hap++)
        {
            bool isDerived = data[hap][locus];
            //build haplotype string
            char digit[2];
            sprintf(digit, "%d", data[hap][i]);
            haplotypeList[hap] += digit;
            string hapStr = haplotypeList[hap];

            if (isDerived)
            {
                //count hapoltypes
                if (derivedHapCount.count(hapStr) == 0) derivedHapCount[hapStr] = 1;
                else derivedHapCount[hapStr]++;
                numDerived++;
            }
            else //ancestral
            {
                if (ancestralHapCount.count(hapStr) == 0) ancestralHapCount[hapStr] = 1;
                else ancestralHapCount[hapStr]++;
                numAncestral++;
            }
        }

        //Write functions to fill in haplotype colors here
        fillColors(derivedHapColor, derivedHapCount, haplotypeList, nhaps, i - stopLeft, derivedCurrentColor, false);
        fillColors(ancestralHapColor, ancestralHapCount, haplotypeList, nhaps, i - stopLeft, ancestralCurrentColor, false);

        current_derived_ehh = (*calc)(derivedHapCount, numDerived, EXACT);
        current_ancestral_ehh = (*calc)(ancestralHapCount, numAncestral, EXACT);

        (*fout) << physicalPos[i] - physicalPos[locus] << "\t"
                << current_derived_ehh << "\t"
                << current_ancestral_ehh << endl;
    }

    delete [] haplotypeList;

    ofstream fout2;
    string outFilenameDer = outFilename + ".der.colormap";
    fout2.open(outFilenameDer.c_str());
    if (fout2.fail())
    {
        cerr << "ERROR: could not open " << outFilenameDer << " for writing.\n";
        throw 1;
    }

    for (int i = 0; i < derivedCount; i++)
    {
        for (int j = 0; j < stopRight - stopLeft + 1; j++)
        {
            fout2 << derivedHapColor[i][j] << " ";
        }
        fout2 << endl;
    }
    fout2.close();

    string outFilenameAnc = outFilename + ".anc.colormap";
    fout2.open(outFilenameAnc.c_str());
    if (fout2.fail())
    {
        cerr << "ERROR: could not open " << outFilenameAnc << " for writing.\n";
        throw 1;
    }

    for (int i = 0; i < nhaps - derivedCount; i++)
    {
        for (int j = 0; j < stopRight - stopLeft + 1; j++)
        {
            fout2 << ancestralHapColor[i][j] << " ";
        }
        fout2 << endl;
    }
    fout2.close();
    return;
}

void query_locus_soft(void *order)
{
    work_order_t *p = (work_order_t *)order;
    short **data = p->hapData->data;
    int nloci = p->hapData->nloci;
    int nhaps = p->hapData->nhaps;
    int *physicalPos = p->mapData->physicalPos;
    double *geneticPos = p->mapData->geneticPos;
    string *locusName = p->mapData->locusName;

    ofstream *flog = p->flog;
    ofstream *fout = p->fout;
    string outFilename = p->filename;
    int SCALE_PARAMETER = p->params->getIntFlag(ARG_GAP_SCALE);
    int MAX_GAP = p->params->getIntFlag(ARG_MAX_GAP);
    double EHH_CUTOFF = p->params->getDoubleFlag(ARG_CUTOFF);
    bool EXACT = p->params->getBoolFlag(ARG_EXACT);

    int locus = p->queryLoc;
    int queryPad = p->params->getIntFlag(ARG_QWIN);
    int stopLeft = locus;
    for (int i = locus - 1; i >= 0; i--)
    {
        if (physicalPos[locus] - physicalPos[i] <= queryPad) stopLeft = i;
    }
    int stopRight = locus;
    for (int i = locus + 1; i < nloci; i++)
    {
        if (physicalPos[i] - physicalPos[locus] <= queryPad) stopRight = i;
    }

    //EHH to the left of the core snp

    double current_ehh1 = 1;
    double current_ehh2d1 = 1;
    double current_ehh12 = 1;

    double previous_ehh1 = 1;
    double previous_ehh2d1 = 1;
    double previous_ehh12 = 1;

    double derivedCount = 0;
    //A list of all the haplotypes
    //Starts with just the focal snp and grows outward
    map<string, int> tempHapCount;
    string *haplotypeList = new string[nhaps];
    for (int hap = 0; hap < nhaps; hap++)
    {
        derivedCount += data[hap][locus];
        char digit[2];
        sprintf(digit, "%d", data[hap][locus]);
        haplotypeList[hap] = digit;
        string hapStr = haplotypeList[hap];
        //count hapoltype freqs
        if (tempHapCount.count(hapStr) == 0) tempHapCount[hapStr] = 1;
        else tempHapCount[hapStr]++;
    }

    triplet_t res = calculateSoft(tempHapCount, nhaps);
    current_ehh1 = res.h1;
    current_ehh2d1 = res.h2dh1;
    current_ehh12 = res.h12;
    previous_ehh1 = res.h1;
    previous_ehh2d1 = res.h2dh1;
    previous_ehh12 = res.h12;

    //cerr << "numHaps: " << nhaps << "\nderivedCounts: " << derivedCount << endl;
    /*
    int **ancestralHapColor = new int*[int(nhaps-derivedCount)];
    for(int i = 0; i < nhaps-derivedCount; i++)
    {
      ancestralHapColor[i] = new int[stopRight-stopLeft+1];
      ancestralHapColor[i][locus-stopLeft] = 0;
    }
    int **derivedHapColor = new int*[int(derivedCount)];
    for(int i = 0; i < derivedCount; i++)
    {
      derivedHapColor[i] = new int[stopRight-stopLeft+1];
      derivedHapColor[i][locus-stopLeft] = 0;
    }
    */
    //cerr << "allocated hap color arrays.\n";

    string *tempResults = new string[locus - stopLeft];
    int tempIndex = locus - stopLeft - 1;
    //int derivedCurrentColor = 0;
    //int ancestralCurrentColor = 0;

    for (int i = locus - 1; i >= stopLeft; i--)
    {
        int numDerived = 0;
        int numAncestral = 0;
        map<string, int> hapCount;

        for (int hap = 0; hap < nhaps; hap++)
        {
            //build haplotype string
            char digit[2];
            sprintf(digit, "%d", data[hap][i]);
            haplotypeList[hap] += digit;
            string hapStr = haplotypeList[hap];

            //count hapoltype freqs
            if (hapCount.count(hapStr) == 0) hapCount[hapStr] = 1;
            else hapCount[hapStr]++;
        }

        //We've now counted all of the unique haplotypes extending out of the core SNP
        res = calculateSoft(hapCount, nhaps);
        current_ehh1 = res.h1;
        current_ehh2d1 = res.h2dh1;
        current_ehh12 = res.h12;
        //Write functions to fill in haplotype colors here
        /*
        fillColors(derivedHapColor, derivedHapCount,haplotypeList, nhaps,tempIndex,derivedCurrentColor,true);
        fillColors(ancestralHapColor, ancestralHapCount,haplotypeList, nhaps,tempIndex,ancestralCurrentColor,true);
        */

        char tempStr[100];
        sprintf(tempStr, "%d\t%f\t%f\t%f", physicalPos[i] - physicalPos[locus], current_ehh1, current_ehh12, current_ehh2d1);
        tempResults[tempIndex] = string(tempStr);
        tempIndex--;
    }

    delete [] haplotypeList;

    for (int i = 0; i < locus - stopLeft; i++)
    {
        (*fout) << tempResults[i] << "\n";
    }
    delete [] tempResults;

    //calculate EHH to the right
    current_ehh1 = 1;
    current_ehh2d1 = 1;
    current_ehh12 = 1;

    previous_ehh1 = 1;
    previous_ehh2d1 = 1;
    previous_ehh12 = 1;

    //A list of all the haplotypes
    //Starts with just the focal snp and grows outward
    tempHapCount.clear();
    haplotypeList = new string[nhaps];
    for (int hap = 0; hap < nhaps; hap++)
    {
        char digit[2];
        sprintf(digit, "%d", data[hap][locus]);
        haplotypeList[hap] = digit;
        string hapStr = haplotypeList[hap];
        //count hapoltype freqs
        if (tempHapCount.count(hapStr) == 0) tempHapCount[hapStr] = 1;
        else tempHapCount[hapStr]++;
    }

    res = calculateSoft(tempHapCount, nhaps);
    current_ehh1 = res.h1;
    current_ehh2d1 = res.h2dh1;
    current_ehh12 = res.h12;
    previous_ehh1 = res.h1;
    previous_ehh2d1 = res.h2dh1;
    previous_ehh12 = res.h12;

    fout->precision(6);
    (*fout) << std::fixed <<  physicalPos[locus] - physicalPos[locus]  << "\t"
            << current_ehh1 << "\t"
            << current_ehh12 << "\t"
            << current_ehh2d1 << endl;

    //while(current_ancestral_ehh > EHH_CUTOFF || current_derived_ehh > EHH_CUTOFF)
    for (int i = locus + 1; i <= stopRight; i++)
    {
        map<string, int> hapCount;

        for (int hap = 0; hap < nhaps; hap++)
        {
            //build haplotype string
            char digit[2];
            sprintf(digit, "%d", data[hap][i]);
            haplotypeList[hap] += digit;
            string hapStr = haplotypeList[hap];

            //count hapoltype freqs
            if (hapCount.count(hapStr) == 0) hapCount[hapStr] = 1;
            else hapCount[hapStr]++;
        }

        //We've now counted all of the unique haplotypes extending out of the core SNP
        res = calculateSoft(hapCount, nhaps);
        current_ehh1 = res.h1;
        current_ehh2d1 = res.h2dh1;
        current_ehh12 = res.h12;

        //Write functions to fill in haplotype colors here
        /*
        fillColors(derivedHapColor, derivedHapCount,haplotypeList, nhaps,i-stopLeft,derivedCurrentColor,false);
        fillColors(ancestralHapColor, ancestralHapCount,haplotypeList, nhaps,i-stopLeft,ancestralCurrentColor,false);
        */

        (*fout) << physicalPos[i] - physicalPos[locus] << "\t"
                << current_ehh1 << "\t"
                << current_ehh12 << "\t"
                << current_ehh2d1 << endl;
    }

    delete [] haplotypeList;

    /*
    ofstream fout2;
    string outFilenameDer = outFilename + ".der.colormap";
    fout2.open(outFilenameDer.c_str());
    if(fout2.fail())
    {
      cerr << "ERROR: could not open " << outFilenameDer << " for writing.\n";
      throw 1;
    }
    for(int i = 0; i < derivedCount; i++)
    {
      for(int j = 0; j < stopRight-stopLeft+1; j++)
      {
        fout2 << derivedHapColor[i][j] << " ";
        }
      fout2 << endl;
    }

    fout2.close();

    string outFilenameAnc = outFilename + ".anc.colormap";
    fout2.open(outFilenameAnc.c_str());
    if(fout2.fail())
    {
      cerr << "ERROR: could not open " << outFilenameAnc << " for writing.\n";
      throw 1;
    }

    for(int i = 0; i < nhaps-derivedCount; i++)
    {
      for(int j = 0; j < stopRight-stopLeft+1; j++)
      {
        fout2 << ancestralHapColor[i][j] << " ";
        }
      fout2 << endl;
    }
    fout2.close();
    */
    return;
}

void fillColors(int **hapColor, map<string, int> &hapCount, string *haplotypeList, int hapListLength, int currentLoc, int &currentColor, bool left)
{
    map<string, int>::iterator i;
    //int numUniqueHaps = hapCount.size();
    //int mostCommonHapCount = 0;
    int nhaps = 0;

    for (i = hapCount.begin(); i != hapCount.end(); i++)
    {
        nhaps += i->second;
    }
    //holds colors for haplotypes that have already been seen in the search
    map<string, int> hapSeen;

    string mostCommonHap = "_NONE_";

    int colorIndex = 0;
    int previousLoc = (left) ? currentLoc + 1 : currentLoc - 1;
    for (int j = 0; j < hapListLength; j++)
    {
        string hapStr = haplotypeList[j];
        if (hapCount.count(hapStr) == 0) continue;

        /*
        for(int h = 0; h < hapListLength; h++)
        {
          if(h == j) cerr << ">>";
          else cerr << "  ";
          cerr << haplotypeList[h] << endl;
        }
        */

        if (hapCount[hapStr] == 1)
        {
            //cerr << "Unique hap\n";
            hapColor[colorIndex][currentLoc] = -1;
            colorIndex++;
            continue;
        }

        //If there was a split in the current haplotype family
        //let the most common continued haplotype keep the color
        //then increment the color for the less common one
        if (familyDidSplit(hapStr, hapCount[hapStr], hapColor, nhaps, colorIndex, previousLoc, mostCommonHap))
        {
            //cerr << "split\n";
            //cerr << hapStr << " " << mostCommonHap << endl;
            if (hapStr.compare(mostCommonHap) == 0)
            {
                //cerr << "common\n";
                hapColor[colorIndex][currentLoc] = hapColor[colorIndex][previousLoc];
            }
            else
            {
                //cerr << "not common ";
                if (hapSeen.count(hapStr) == 0) //not seen
                {
                    //cerr << "not seen\n";
                    hapColor[colorIndex][currentLoc] = ++currentColor;
                    hapSeen[hapStr] = currentColor;
                }
                else // seen
                {
                    //cerr << "seen\n";
                    hapColor[colorIndex][currentLoc] = hapSeen[hapStr];
                }
            }
        }
        //Family did not split, so keep the color it had
        else
        {
            //cerr << "no split\n";
            hapColor[colorIndex][currentLoc] = hapColor[colorIndex][previousLoc];
        }
        colorIndex++;

        //string junk;
        //cin >> junk;
    }
    return;
}

bool familyDidSplit(const string &hapStr, const int hapCount, int **hapColor, const int nhaps, const int colorIndex, const int previousLoc, string &mostCommonHap)
{
    //cerr << "most common hap: " << mostCommonHap << endl;
    int previousColor = hapColor[colorIndex][previousLoc];
    int numPrevColor = 0;

    for (int i = 0; i < nhaps; i++)
    {
        if (hapColor[i][previousLoc] == previousColor) numPrevColor++;
    }

    //cerr << "numPrevColor: " << numPrevColor << "\nhapCount: " << hapCount << endl;

    if (numPrevColor == hapCount) return false;

    if ( hapCount > double(numPrevColor) / 2.0  )
    {
        mostCommonHap = hapStr;
        return true;
    }
    else if (mostCommonHap.compare("_NONE_") == 0 && hapCount == double(numPrevColor) / 2.0)
    {
        mostCommonHap = hapStr;
        return true;
    }
    else return true;
}

void calc_ihs(void *order)
{
    work_order_t *p = (work_order_t *)order;
    short **data = p->hapData->data;
    int nloci = p->hapData->nloci;
    int nhaps = p->hapData->nhaps;
    int *physicalPos = p->mapData->physicalPos;
    double *geneticPos = p->mapData->geneticPos;
    string *locusName = p->mapData->locusName;
    int start = p->first_index;
    int stop = p->last_index;
    double *ihs = p->ihs;
    double *ihh1 = p->ihh1;
    double *ihh2 = p->ihh2;
    double *freq = p->freq;
    ofstream *flog = p->flog;
    Bar *pbar = p->bar;

    int SCALE_PARAMETER = p->params->getIntFlag(ARG_GAP_SCALE);
    int MAX_GAP = p->params->getIntFlag(ARG_MAX_GAP);
    double EHH_CUTOFF = p->params->getDoubleFlag(ARG_CUTOFF);
    bool EXACT = p->params->getBoolFlag(ARG_EXACT);
    double FILTER = p->params->getDoubleFlag(ARG_FREQ);


    double (*calc)(map<string, int> &, int, bool) = p->calc;

    int step = (stop - start) / (pbar->totalTicks);
    if (step == 0) step = 1;

    for (int locus = start; locus < stop; locus++)
    {
        if (locus % step == 0) advanceBar(*pbar, double(step));

        ihs[locus] = 0;
        freq[locus] = MISSING;
        ihh1[locus] = MISSING;
        ihh2[locus] = MISSING;

        //EHH to the left of the core snp
        double current_derived_ehh = 1;
        double current_ancestral_ehh = 1;
        double previous_derived_ehh = 1;
        double previous_ancestral_ehh = 1;
        int currentLocus = locus;
        int nextLocus = locus - 1;
        bool skipLocus = 0;
        double derived_ihh = 0;
        double ancestral_ihh = 0;
        double derivedCount = 0;
        //A list of all the haplotypes
        //Starts with just the focal snp and grows outward
        string *haplotypeList = new string[nhaps];
        for (int hap = 0; hap < nhaps; hap++)
        {
            derivedCount += data[hap][locus];
            char digit[2];
            sprintf(digit, "%d", data[hap][locus]);
            haplotypeList[hap] = digit;
        }

        //If the focal snp has MAF < FILTER, then skip this locus
        double alleleFreq = double(derivedCount) / double(nhaps);
        if (alleleFreq < FILTER || alleleFreq > 1 - FILTER)
        {
            pthread_mutex_lock(&mutex_log);
            (*flog) << "WARNING: Locus " << locusName[locus]
                    << " has MAF < " << FILTER << ". Skipping calcualtion at this locus.\n";
            pthread_mutex_unlock(&mutex_log);
            ihs[locus] = MISSING;
            skipLocus = 0;
            continue;
        }

        while (current_derived_ehh > EHH_CUTOFF || current_ancestral_ehh > EHH_CUTOFF)
        {
            if (nextLocus < 0)
            {
                break;
            }
            else if (physicalPos[currentLocus] - physicalPos[nextLocus] > MAX_GAP)
            {
                pthread_mutex_lock(&mutex_log);
                (*flog) << "WARNING: Reached a gap of " << physicalPos[currentLocus] - physicalPos[nextLocus]
                        << "bp > " << MAX_GAP << "bp. Skipping calcualtion at this locus.\n";
                pthread_mutex_unlock(&mutex_log);
                skipLocus = 1;
                break;
            }

            //Check to see if the gap between the markers is huge, if so, scale it in an ad hoc way as in
            //Voight et al. (2006)
            double scale = double(SCALE_PARAMETER) / double(physicalPos[currentLocus] - physicalPos[nextLocus]);
            if (scale > 1) scale = 1;

            currentLocus = nextLocus;
            nextLocus--;

            int numDerived = 0;
            int numAncestral = 0;
            map<string, int> ancestralHapCount;
            map<string, int> derivedHapCount;

            for (int hap = 0; hap < nhaps; hap++)
            {
                bool isDerived = data[hap][locus];
                //build haplotype string
                char digit[2];
                sprintf(digit, "%d", data[hap][currentLocus]);
                haplotypeList[hap] += digit;
                string hapStr = haplotypeList[hap];

                if (isDerived)
                {
                    //count derived hapoltype
                    if (derivedHapCount.count(hapStr) == 0) derivedHapCount[hapStr] = 1;
                    else derivedHapCount[hapStr]++;
                    numDerived++;
                }
                else
                {
                    //count ancestral haplotype
                    if (ancestralHapCount.count(hapStr) == 0) ancestralHapCount[hapStr] = 1;
                    else ancestralHapCount[hapStr]++;
                    numAncestral++;
                }
            }

            //We've now counted all of the unique haplotypes extending out of the core SNP
            //If locus is monomorphic, shoot a warning and skip locus
            //This probably isn't necessary any more
            if (numDerived == 0 || numAncestral == 0)
            {
                pthread_mutex_lock(&mutex_log);
                (*flog) << "WARNING: locus " << locusName[locus]
                        << " (number " << locus + 1 << ") is monomorphic.  Skipping calcualtion at this locus.\n";
                pthread_mutex_unlock(&mutex_log);
                skipLocus = 1;
                break;
            }

            if (current_derived_ehh > EHH_CUTOFF)
            {
                current_derived_ehh = (*calc)(derivedHapCount, numDerived, EXACT);

                //directly calculate ihs, iteratively
                //Trapezoid rule
                derived_ihh += 0.5 * scale * (geneticPos[currentLocus + 1] - geneticPos[currentLocus]) * (current_derived_ehh + previous_derived_ehh);
                previous_derived_ehh = current_derived_ehh;
            }

            if (current_ancestral_ehh > EHH_CUTOFF)
            {
                current_ancestral_ehh = (*calc)(ancestralHapCount, numAncestral, EXACT);

                //directly calculate ihs, iteratively
                //Trapezoid rule
                ancestral_ihh += 0.5 * scale * (geneticPos[currentLocus + 1] - geneticPos[currentLocus]) * (current_ancestral_ehh + previous_ancestral_ehh);
                previous_ancestral_ehh = current_ancestral_ehh;
            }
        }

        delete [] haplotypeList;

        if (skipLocus == 1)
        {
            ihs[locus] = MISSING;
            skipLocus = 0;
            continue;
        }

        //calculate EHH to the right
        current_derived_ehh = 1;
        current_ancestral_ehh = 1;
        previous_derived_ehh = 1;
        previous_ancestral_ehh = 1;
        currentLocus = locus;
        nextLocus = locus + 1;
        skipLocus = 0;
        //A list of all the haplotypes
        //Starts with just the focal snp and grows outward
        haplotypeList = new string[nhaps];
        for (int hap = 0; hap < nhaps; hap++)
        {
            char digit[2];
            sprintf(digit, "%d", data[hap][locus]);
            haplotypeList[hap] = digit;
        }

        while (current_ancestral_ehh > EHH_CUTOFF || current_derived_ehh > EHH_CUTOFF)
        {
            if (nextLocus > nloci - 1)
            {
                break;
            }
            else if (physicalPos[nextLocus] - physicalPos[currentLocus] > MAX_GAP)
            {
                pthread_mutex_lock(&mutex_log);
                (*flog) << "WARNING: Reached a gap of " << physicalPos[nextLocus] - physicalPos[currentLocus]
                        << "bp > " << MAX_GAP << "bp. Skipping calcualtion at this locus.\n";
                pthread_mutex_unlock(&mutex_log);
                skipLocus = 1;
                break;
            }

            double scale = double(SCALE_PARAMETER) / double(physicalPos[nextLocus] - physicalPos[currentLocus]);
            if (scale > 1) scale = 1;

            currentLocus = nextLocus;
            nextLocus++;

            int numDerived = 0;
            int numAncestral = 0;
            map<string, int> ancestralHapCount;
            map<string, int> derivedHapCount;
            for (int hap = 0; hap < nhaps; hap++)
            {
                bool isDerived = data[hap][locus];
                //build haplotype string
                char digit[2];
                sprintf(digit, "%d", data[hap][currentLocus]);
                haplotypeList[hap] += digit;
                string hapStr = haplotypeList[hap];

                if (isDerived)
                {
                    //count hapoltypes
                    if (derivedHapCount.count(hapStr) == 0) derivedHapCount[hapStr] = 1;
                    else derivedHapCount[hapStr]++;
                    numDerived++;
                }
                else //ancestral
                {
                    if (ancestralHapCount.count(hapStr) == 0) ancestralHapCount[hapStr] = 1;
                    else ancestralHapCount[hapStr]++;
                    numAncestral++;
                }
            }

            //We've now counted all of the unique haplotypes extending out of the core SNP
            //If there are no derived alleles at a locus, shoot a warning and skip locus
            if (numDerived == 0 || numAncestral == 0)
            {
                //(*flog) << "WARNING: locus " << locusName[locus]
                //   << " (number " << locus+1 << ") is monomorphic.  Skipping calcualtion at this locus.\n";
                skipLocus = 1;
                break;
            }

            if (current_derived_ehh > EHH_CUTOFF)
            {
                current_derived_ehh = (*calc)(derivedHapCount, numDerived, EXACT);

                //directly calculate ihs, iteratively
                //Trapezoid rule
                derived_ihh += 0.5 * scale * (geneticPos[currentLocus] - geneticPos[currentLocus - 1]) * (current_derived_ehh + previous_derived_ehh);
                previous_derived_ehh = current_derived_ehh;
            }

            if (current_ancestral_ehh > EHH_CUTOFF)
            {
                current_ancestral_ehh = (*calc)(ancestralHapCount, numAncestral, EXACT);

                //directly calculate ihs, iteratively
                //Trapezoid rule
                ancestral_ihh += 0.5 * scale * (geneticPos[currentLocus] - geneticPos[currentLocus - 1]) * (current_ancestral_ehh + previous_ancestral_ehh);
                previous_ancestral_ehh = current_ancestral_ehh;
            }
        }

        delete [] haplotypeList;

        if (skipLocus == 1)
        {
            ihs[locus] = MISSING;
            skipLocus = 0;
            continue;
        }

        if (ihs[locus] != MISSING)
        {
            ihh1[locus] = derived_ihh;
            ihh2[locus] = ancestral_ihh;
            ihs[locus] = log(derived_ihh / ancestral_ihh);
            freq[locus] = double(derivedCount) / double(nhaps);
        }
    }
}

void calc_soft_ihs(void *order)
{
    work_order_t *p = (work_order_t *)order;
    short **data = p->hapData->data;
    int nloci = p->hapData->nloci;
    int nhaps = p->hapData->nhaps;
    int *physicalPos = p->mapData->physicalPos;
    double *geneticPos = p->mapData->geneticPos;
    string *locusName = p->mapData->locusName;
    int start = p->first_index;
    int stop = p->last_index;
    double *h1 = p->ihh1;
    double *h2dh1 = p->ihh2;
    double *h12 = p->ihs;
    double *freq = p->freq;
    ofstream *flog = p->flog;
    Bar *pbar = p->bar;

    int SCALE_PARAMETER = p->params->getIntFlag(ARG_GAP_SCALE);
    int MAX_GAP = p->params->getIntFlag(ARG_MAX_GAP);
    double EHH_CUTOFF = p->params->getDoubleFlag(ARG_CUTOFF);
    bool EXACT = p->params->getBoolFlag(ARG_EXACT);
    double FILTER = p->params->getDoubleFlag(ARG_FREQ);

    int step = (stop - start) / (pbar->totalTicks);
    if (step == 0) step = 1;

    for (int locus = start; locus < stop; locus++)
    {
        if (locus % step == 0) advanceBar(*pbar, double(step));

        freq[locus] = MISSING;
        h1[locus] = -1;
        h2dh1[locus] = -1;
        h12[locus] = -1;

        //EHH to the left of the core snp
        double current_ehh1 = 1;
        double current_ehh2d1 = 1;
        double current_ehh12 = 1;

        double previous_ehh1 = 1;
        double previous_ehh2d1 = 1;
        double previous_ehh12 = 1;

        int currentLocus = locus;
        int nextLocus = locus - 1;
        bool skipLocus = 0;
        double ihh1 = 0;
        double ihh2d1 = 0;
        double ihh12 = 0;
        double derivedCount = 0;
        //A list of all the haplotypes
        //Starts with just the focal snp and grows outward
        map<string, int> tempHapCount;
        string *haplotypeList = new string[nhaps];
        for (int hap = 0; hap < nhaps; hap++)
        {
            derivedCount += data[hap][locus];
            char digit[2];
            sprintf(digit, "%d", data[hap][locus]);
            haplotypeList[hap] = digit;
            string hapStr = haplotypeList[hap];
            //count hapoltype freqs
            if (tempHapCount.count(hapStr) == 0) tempHapCount[hapStr] = 1;
            else tempHapCount[hapStr]++;
        }

        triplet_t res = calculateSoft(tempHapCount, nhaps);
        current_ehh1 = res.h1;
        current_ehh2d1 = res.h2dh1;
        current_ehh12 = res.h12;
        previous_ehh1 = res.h1;
        previous_ehh2d1 = res.h2dh1;
        previous_ehh12 = res.h12;

        while (current_ehh1 > EHH_CUTOFF)
        {
            if (nextLocus < 0)
            {
                break;
            }
            else if (physicalPos[currentLocus] - physicalPos[nextLocus] > MAX_GAP)
            {
                pthread_mutex_lock(&mutex_log);
                (*flog) << "WARNING: Reached a gap of " << physicalPos[currentLocus] - physicalPos[nextLocus] << "bp > " << MAX_GAP << "bp. Skipping calcualtion at this locus.\n";
                pthread_mutex_unlock(&mutex_log);
                skipLocus = 1;
                break;
            }

            //Check to see if the gap between the markers is huge, if so, scale it in an ad hoc way as in
            //Voight et al. (2006)
            double scale = double(SCALE_PARAMETER) / double(physicalPos[currentLocus] - physicalPos[nextLocus]);
            if (scale > 1) scale = 1;

            currentLocus = nextLocus;
            nextLocus--;

            map<string, int> hapCount;

            for (int hap = 0; hap < nhaps; hap++)
            {
                //build haplotype string
                char digit[2];
                sprintf(digit, "%d", data[hap][currentLocus]);
                haplotypeList[hap] += digit;
                string hapStr = haplotypeList[hap];

                //count hapoltype freqs
                if (hapCount.count(hapStr) == 0) hapCount[hapStr] = 1;
                else hapCount[hapStr]++;
            }

            //We've now counted all of the unique haplotypes extending out of the core SNP
            res = calculateSoft(hapCount, nhaps);
            current_ehh1 = res.h1;
            current_ehh2d1 = res.h2dh1;
            current_ehh12 = res.h12;

            //directly calculate integral, iteratively
            //Trapezoid rule
            ihh1 += 0.5 * scale * (geneticPos[currentLocus + 1] - geneticPos[currentLocus]) * (current_ehh1 + previous_ehh1);
            previous_ehh1 = current_ehh1;
            ihh2d1 += 0.5 * scale * (geneticPos[currentLocus + 1] - geneticPos[currentLocus]) * (current_ehh2d1 + previous_ehh2d1);
            previous_ehh2d1 = current_ehh2d1;
            ihh12 += 0.5 * scale * (geneticPos[currentLocus + 1] - geneticPos[currentLocus]) * (current_ehh12 + previous_ehh12);
            previous_ehh12 = current_ehh12;
        }

        delete [] haplotypeList;

        if (skipLocus == 1)
        {
            h1[locus] = MISSING;
            h2dh1[locus] = MISSING;
            h12[locus] = MISSING;
            skipLocus = 0;
            continue;
        }

        //calculate EHH to the right
        current_ehh1 = 1;
        current_ehh2d1 = 1;
        current_ehh12 = 1;

        previous_ehh1 = 1;
        previous_ehh2d1 = 1;
        previous_ehh12 = 1;

        ihh1 = 0;
        ihh2d1 = 0;
        ihh12 = 0;

        currentLocus = locus;
        nextLocus = locus + 1;
        skipLocus = 0;
        //A list of all the haplotypes
        //Starts with just the focal snp and grows outward
        tempHapCount.clear();
        haplotypeList = new string[nhaps];
        for (int hap = 0; hap < nhaps; hap++)
        {
            char digit[2];
            sprintf(digit, "%d", data[hap][locus]);
            haplotypeList[hap] = digit;
            string hapStr = haplotypeList[hap];
            //count hapoltype freqs
            if (tempHapCount.count(hapStr) == 0) tempHapCount[hapStr] = 1;
            else tempHapCount[hapStr]++;
        }

        res = calculateSoft(tempHapCount, nhaps);
        current_ehh1 = res.h1;
        current_ehh2d1 = res.h2dh1;
        current_ehh12 = res.h12;
        previous_ehh1 = res.h1;
        previous_ehh2d1 = res.h2dh1;
        previous_ehh12 = res.h12;

        while (current_ehh1 > EHH_CUTOFF)
        {
            if (nextLocus > nloci - 1)
            {
                break;
            }
            else if (physicalPos[nextLocus] - physicalPos[currentLocus] > MAX_GAP)
            {
                pthread_mutex_lock(&mutex_log);
                (*flog) << "WARNING: Reached a gap of " << physicalPos[nextLocus] - physicalPos[currentLocus] << "bp > " << MAX_GAP << "bp. Skipping calcualtion at this locus.\n";
                pthread_mutex_unlock(&mutex_log);
                skipLocus = 1;
                break;
            }

            double scale = double(SCALE_PARAMETER) / double(physicalPos[nextLocus] - physicalPos[currentLocus]);
            if (scale > 1) scale = 1;

            currentLocus = nextLocus;
            nextLocus++;

            map<string, int> hapCount;

            for (int hap = 0; hap < nhaps; hap++)
            {
                //build haplotype string
                char digit[2];
                sprintf(digit, "%d", data[hap][currentLocus]);
                haplotypeList[hap] += digit;
                string hapStr = haplotypeList[hap];

                //count hapoltype freqs
                if (hapCount.count(hapStr) == 0) hapCount[hapStr] = 1;
                else hapCount[hapStr]++;
            }

            //We've now counted all of the unique haplotypes extending out of the core SNP
            res = calculateSoft(hapCount, nhaps);
            current_ehh1 = res.h1;
            current_ehh2d1 = res.h2dh1;
            current_ehh12 = res.h12;

            //directly calculate integral, iteratively
            //Trapezoid rule
            ihh1 += 0.5 * scale * (geneticPos[currentLocus] - geneticPos[currentLocus - 1]) * (current_ehh1 + previous_ehh1);
            previous_ehh1 = current_ehh1;
            ihh2d1 += 0.5 * scale * (geneticPos[currentLocus] - geneticPos[currentLocus - 1]) * (current_ehh2d1 + previous_ehh2d1);
            previous_ehh2d1 = current_ehh2d1;
            ihh12 += 0.5 * scale * (geneticPos[currentLocus] - geneticPos[currentLocus - 1]) * (current_ehh12 + previous_ehh12);
            previous_ehh12 = current_ehh12;
        }

        delete [] haplotypeList;

        if (skipLocus == 1)
        {
            h1[locus] = MISSING;
            h2dh1[locus] = MISSING;
            h12[locus] = MISSING;
            skipLocus = 0;
            continue;
        }

        if (h12[locus] != MISSING)
        {
            h1[locus] = ihh1;
            h2dh1[locus] = ihh2d1;
            h12[locus] = ihh12;
            freq[locus] = double(derivedCount) / double(nhaps);
        }
    }
}


void calc_xpihh(void *order)
{
    work_order_t *p = (work_order_t *)order;

    short **data1 = p->hapData1->data;
    int nhaps1 = p->hapData1->nhaps;
    double *ihh1 = p->ihh1;
    double *freq1 = p->freq1;

    short **data2 = p->hapData2->data;
    int nhaps2 = p->hapData2->nhaps;
    double *ihh2 = p->ihh2;
    double *freq2 = p->freq2;

    int nloci = p->mapData->nloci;
    int *physicalPos = p->mapData->physicalPos;
    double *geneticPos = p->mapData->geneticPos;
    string *locusName = p->mapData->locusName;

    int start = p->first_index;
    int stop = p->last_index;

    ofstream *flog = p->flog;
    Bar *pbar = p->bar;

    int SCALE_PARAMETER = p->params->getIntFlag(ARG_GAP_SCALE);
    int MAX_GAP = p->params->getIntFlag(ARG_MAX_GAP);
    double EHH_CUTOFF = p->params->getDoubleFlag(ARG_CUTOFF);
    bool EXACT = p->params->getBoolFlag(ARG_EXACT);

    //Weird offset thing mentioned in Sabetti et al. (2007) that is apparently used to calculate iHH
    //subtract this from EHH when integrating
    //double offset = EHH_CUTOFF-(1.0/double(nhaps));

    int step = (stop - start) / (pbar->totalTicks);
    if (step == 0) step = 1;

    for (int locus = start; locus < stop; locus++)
    {
        if (locus % step == 0) advanceBar(*pbar, double(step));

        ihh1[locus] = 0;
        ihh2[locus] = 0;

        freq1[locus] = MISSING;
        freq2[locus] = MISSING;

        //EHH to the left of the core snp
        double current_pooled_ehh = 1;
        double previous_pooled_ehh = 1;
        double derivedCountPooled = 0;

        double current_pop1_ehh = 1;
        double previous_pop1_ehh = 1;
        double ihhPop1 = 0;
        double derivedCount1 = 0;

        double current_pop2_ehh = 1;
        double previous_pop2_ehh = 1;
        double ihhPop2 = 0;
        double derivedCount2 = 0;

        int currentLocus = locus;
        int nextLocus = locus - 1;
        bool skipLocus = 0;

        //A list of all the haplotypes
        //Starts with just the focal snp and grows outward
        string *haplotypeList1, *haplotypeList2, *haplotypeListPooled;
        haplotypeList1 = new string[nhaps1];
        haplotypeList2 = new string[nhaps2];
        haplotypeListPooled = new string[nhaps1 + nhaps2];
        for (int hap = 0; hap < nhaps1 + nhaps2; hap++)
        {
            char digit[2];
            //Pop1
            if (hap < nhaps1)
            {
                sprintf(digit, "%d", data1[hap][locus]);
                haplotypeList1[hap] = digit;
                derivedCount1 += data1[hap][locus];
            }
            //Pop2
            else
            {
                sprintf(digit, "%d", data2[hap - nhaps1][locus]);
                haplotypeList2[hap - nhaps1] = digit;
                derivedCount2 += data2[hap - nhaps1][locus];
            }

            //Pooled
            haplotypeListPooled[hap] = digit;
        }

        derivedCountPooled = derivedCount1 + derivedCount2;

        //when calculating xp-ehh, ehh does not necessarily start at 1
        if (EXACT)
        {
            current_pop1_ehh = (derivedCount1 > 1) ? nCk(derivedCount1, 2) / nCk(nhaps1, 2) : 0;
            current_pop1_ehh += (nhaps1 - derivedCount1 > 1) ? nCk(nhaps1 - derivedCount1, 2) / nCk(nhaps1, 2) : 0;
            previous_pop1_ehh = current_pop1_ehh;

            current_pop2_ehh = (derivedCount2 > 1) ? nCk(derivedCount2, 2) / nCk(nhaps2, 2) : 0;
            current_pop2_ehh += (nhaps2 - derivedCount2 > 1) ? nCk(nhaps2 - derivedCount2, 2) / nCk(nhaps2, 2) : 0;
            previous_pop2_ehh = current_pop2_ehh;

            current_pooled_ehh = (derivedCountPooled > 1) ? nCk(derivedCountPooled, 2) / nCk(nhaps1 + nhaps2, 2) : 0;
            current_pooled_ehh += (nhaps1 + nhaps2 - derivedCountPooled > 1) ? nCk(nhaps1 + nhaps2 - derivedCountPooled, 2) / nCk(nhaps1 + nhaps2, 2) : 0;
            previous_pooled_ehh = current_pooled_ehh;
        }
        else
        {
            double f = double(derivedCount1) / double(nhaps1);
            current_pop1_ehh = f * f + (1 - f) * (1 - f);
            previous_pop1_ehh = current_pop1_ehh;

            f = double(derivedCount2) / double(nhaps2);
            current_pop2_ehh = f * f + (1 - f) * (1 - f);
            previous_pop2_ehh = current_pop2_ehh;

            f = double(derivedCountPooled) / double(nhaps1 + nhaps2);
            current_pooled_ehh = f * f + (1 - f) * (1 - f);
            previous_pooled_ehh = current_pooled_ehh;
        }

        while (current_pooled_ehh > EHH_CUTOFF)
        {
            if (nextLocus < 0) break;
            else if (physicalPos[currentLocus] - physicalPos[nextLocus] > MAX_GAP)
            {
                pthread_mutex_lock(&mutex_log);
                (*flog) << "WARNING: Reached a gap of " << physicalPos[currentLocus] - physicalPos[nextLocus] << "bp > " << MAX_GAP << "bp. Skipping calcualtion at this locus.\n";
                pthread_mutex_unlock(&mutex_log);
                skipLocus = 1;
                break;
            }

            //Check to see if the gap between the markers is huge, if so, scale it in an ad hoc way as in
            //Voight, et al. paper
            double scale = double(SCALE_PARAMETER) / double(physicalPos[currentLocus] - physicalPos[nextLocus]);
            if (scale > 1) scale = 1;

            currentLocus = nextLocus;
            nextLocus--;

            map<string, int> hapCount1;
            map<string, int> hapCount2;
            map<string, int> hapCountPooled;

            //build haplotype strings
            for (int hap = 0; hap < nhaps1 + nhaps2; hap++)
            {
                char digit[2];
                string hapStr;
                if (hap < nhaps1) //Pop1
                {
                    sprintf(digit, "%d", data1[hap][currentLocus]);
                    haplotypeList1[hap] += digit;
                    hapStr = haplotypeList1[hap];
                    if (hapCount1.count(hapStr) == 0) hapCount1[hapStr] = 1;
                    else hapCount1[hapStr]++;
                }
                else //Pop2
                {
                    sprintf(digit, "%d", data2[hap - nhaps1][currentLocus]);
                    haplotypeList2[hap - nhaps1] += digit;
                    hapStr = haplotypeList2[hap - nhaps1];
                    if (hapCount2.count(hapStr) == 0) hapCount2[hapStr] = 1;
                    else hapCount2[hapStr]++;
                }

                //Pooled
                haplotypeListPooled[hap] += digit;
                hapStr = haplotypeListPooled[hap];
                if (hapCountPooled.count(hapStr) == 0) hapCountPooled[hapStr] = 1;
                else hapCountPooled[hapStr]++;
            }

            current_pop1_ehh = calculateHomozygosity(hapCount1, nhaps1, EXACT);
            current_pop2_ehh = calculateHomozygosity(hapCount2, nhaps2, EXACT);
            current_pooled_ehh = calculateHomozygosity(hapCountPooled, nhaps1 + nhaps2, EXACT);

            //directly calculate ihh, iteratively
            //Trapezoid rule
            ihhPop1 += 0.5 * scale * (geneticPos[currentLocus + 1] - geneticPos[currentLocus]) * (current_pop1_ehh + previous_pop1_ehh);
            previous_pop1_ehh = current_pop1_ehh;

            ihhPop2 += 0.5 * scale * (geneticPos[currentLocus + 1] - geneticPos[currentLocus]) * (current_pop2_ehh + previous_pop2_ehh);
            previous_pop2_ehh = current_pop2_ehh;

            previous_pooled_ehh = current_pooled_ehh;
        }

        delete [] haplotypeList1;
        delete [] haplotypeList2;
        delete [] haplotypeListPooled;

        if (skipLocus == 1)
        {
            ihh1[locus] = MISSING;
            ihh2[locus] = MISSING;
            skipLocus = 0;
            continue;
        }

        //calculate EHH to the right

        current_pooled_ehh = 1;
        previous_pooled_ehh = 1;
        derivedCountPooled = 0;

        current_pop1_ehh = 1;
        previous_pop1_ehh = 1;
        derivedCount1 = 0;

        current_pop2_ehh = 1;
        previous_pop2_ehh = 1;
        derivedCount2 = 0;

        currentLocus = locus;
        nextLocus = locus + 1;
        skipLocus = 0;

        //A list of all the haplotypes
        //Starts with just the focal snp and grows outward
        haplotypeList1 = new string[nhaps1];
        haplotypeList2 = new string[nhaps2];
        haplotypeListPooled = new string[nhaps1 + nhaps2];
        for (int hap = 0; hap < nhaps1 + nhaps2; hap++)
        {
            char digit[2];
            //Pop1
            if (hap < nhaps1)
            {
                sprintf(digit, "%d", data1[hap][locus]);
                haplotypeList1[hap] = digit;
                derivedCount1 += data1[hap][locus];
            }
            //Pop2
            else
            {
                sprintf(digit, "%d", data2[hap - nhaps1][locus]);
                haplotypeList2[hap - nhaps1] = digit;
                derivedCount2 += data2[hap - nhaps1][locus];
            }

            //Pooled
            haplotypeListPooled[hap] = digit;
        }

        derivedCountPooled = derivedCount1 + derivedCount2;

        //when calculating xp-ehh, ehh does not necessarily start at 1
        if (EXACT)
        {
            current_pop1_ehh = (derivedCount1 > 1) ? nCk(derivedCount1, 2) / nCk(nhaps1, 2) : 0;
            current_pop1_ehh += (nhaps1 - derivedCount1 > 1) ? nCk(nhaps1 - derivedCount1, 2) / nCk(nhaps1, 2) : 0;
            previous_pop1_ehh = current_pop1_ehh;

            current_pop2_ehh = (derivedCount2 > 1) ? nCk(derivedCount2, 2) / nCk(nhaps2, 2) : 0;
            current_pop2_ehh += (nhaps2 - derivedCount2 > 1) ? nCk(nhaps2 - derivedCount2, 2) / nCk(nhaps2, 2) : 0;
            previous_pop2_ehh = current_pop2_ehh;

            current_pooled_ehh = (derivedCountPooled > 1) ? nCk(derivedCountPooled, 2) / nCk(nhaps1 + nhaps2, 2) : 0;
            current_pooled_ehh += (nhaps1 + nhaps2 - derivedCountPooled > 1) ? nCk(nhaps1 + nhaps2 - derivedCountPooled, 2) / nCk(nhaps1 + nhaps2, 2) : 0;
            previous_pooled_ehh = current_pooled_ehh;
        }
        else
        {
            double f = double(derivedCount1) / double(nhaps1);
            current_pop1_ehh = f * f + (1 - f) * (1 - f);
            previous_pop1_ehh = current_pop1_ehh;

            f = double(derivedCount2) / double(nhaps2);
            current_pop2_ehh = f * f + (1 - f) * (1 - f);
            previous_pop2_ehh = current_pop2_ehh;

            f = double(derivedCountPooled) / double(nhaps1 + nhaps2);
            current_pooled_ehh = f * f + (1 - f) * (1 - f);
            previous_pooled_ehh = current_pooled_ehh;
        }


        while (current_pooled_ehh > EHH_CUTOFF)
        {
            if (nextLocus > nloci - 1) break;
            else if (physicalPos[nextLocus] - physicalPos[currentLocus] > MAX_GAP)
            {
                pthread_mutex_lock(&mutex_log);
                (*flog) << "WARNING: Reached a gap of " << physicalPos[nextLocus] - physicalPos[currentLocus] << "bp > " << MAX_GAP << "bp. Skipping calcualtion at this locus.\n";
                pthread_mutex_unlock(&mutex_log);
                skipLocus = 1;
                break;
            }

            double scale = double(SCALE_PARAMETER) / double(physicalPos[nextLocus] - physicalPos[currentLocus]);
            if (scale > 1) scale = 1;

            currentLocus = nextLocus;
            nextLocus++;

            map<string, int> hapCount1;
            map<string, int> hapCount2;
            map<string, int> hapCountPooled;

            //build haplotype strings
            for (int hap = 0; hap < nhaps1 + nhaps2; hap++)
            {
                char digit[2];
                string hapStr;

                //pop1
                if (hap < nhaps1)
                {
                    sprintf(digit, "%d", data1[hap][currentLocus]);
                    haplotypeList1[hap] += digit;
                    hapStr = haplotypeList1[hap];
                    if (hapCount1.count(hapStr) == 0) hapCount1[hapStr] = 1;
                    else hapCount1[hapStr]++;
                }
                //Pop2
                else
                {
                    sprintf(digit, "%d", data2[hap - nhaps1][currentLocus]);
                    haplotypeList2[hap - nhaps1] += digit;
                    hapStr = haplotypeList2[hap - nhaps1];
                    if (hapCount2.count(hapStr) == 0) hapCount2[hapStr] = 1;
                    else hapCount2[hapStr]++;
                }
                //Pooled
                haplotypeListPooled[hap] += digit;
                hapStr = haplotypeListPooled[hap];
                if (hapCountPooled.count(hapStr) == 0) hapCountPooled[hapStr] = 1;
                else hapCountPooled[hapStr]++;
            }

            current_pop1_ehh = calculateHomozygosity(hapCount1, nhaps1, EXACT);
            current_pop2_ehh = calculateHomozygosity(hapCount2, nhaps2, EXACT);
            current_pooled_ehh = calculateHomozygosity(hapCountPooled, nhaps1 + nhaps2, EXACT);

            //directly calculate ihh1, iteratively
            //Trapezoid rule
            ihhPop1 += 0.5 * scale * (geneticPos[currentLocus] - geneticPos[currentLocus - 1]) * (current_pop1_ehh + previous_pop1_ehh);
            previous_pop1_ehh = current_pop1_ehh;

            ihhPop2 += 0.5 * scale * (geneticPos[currentLocus] - geneticPos[currentLocus - 1]) * (current_pop2_ehh + previous_pop2_ehh);
            previous_pop2_ehh = current_pop2_ehh;

            previous_pooled_ehh = current_pooled_ehh;
        }

        delete [] haplotypeList1;
        delete [] haplotypeList2;
        delete [] haplotypeListPooled;

        if (skipLocus == 1)
        {
            ihh1[locus] = MISSING;
            ihh2[locus] = MISSING;
            skipLocus = 0;
            continue;
        }

        if (ihh1[locus] != MISSING)
        {
            ihh1[locus] = ihhPop1;
            freq1[locus] = double(derivedCount1) / double(nhaps1);
        }

        if (ihh2[locus] != MISSING)
        {
            ihh2[locus] = ihhPop2;
            freq2[locus] = double(derivedCount2) / double(nhaps2);
        }
    }
}


double calculateHomozygosity(map<string, int> &count, int total, bool EXACT)
{
    double freq = 0;
    double homozygosity = 0;
    map<string, int>::iterator it;
    for (it = count.begin(); it != count.end(); it++)
    {
        if (EXACT)
        {
            homozygosity += (it->second > 1) ? nCk(it->second, 2) / nCk(total, 2) : 0;
        }
        else
        {
            freq = double(it->second) / double(total);
            homozygosity += freq * freq;
        }
    }

    return homozygosity;
}


triplet_t calculateSoft(map<string, int> &count, int total)
{
    triplet_t res;
    res.h1 = 0;
    res.h2dh1 = 0;
    res.h12 = 0;

    double first = 0;
    double second = 0;
    double freq = 0;
    double homozygosity = 0;
    map<string, int>::iterator it;
    for (it = count.begin(); it != count.end(); it++)
    {
        homozygosity += (it->second > 1) ? nCk(it->second, 2) / nCk(total, 2) : 0;

        if (it->second > first)
        {
            second = first;
            first = it->second;
        }
        else if (it->second > second)
        {
            second = it->second;
        }
    }

    double firstFreq = (first > 1) ? nCk(first, 2) / nCk(total, 2) : 0;
    double secondFreq = (second > 1) ? nCk(second, 2) / nCk(total, 2) : 0;
    double comboFreq = ((first + second) > 1) ? nCk((first + second), 2) / nCk(total, 2) : 0;

    res.h1 = homozygosity;
    res.h2dh1 = (homozygosity - firstFreq) / homozygosity;
    res.h12 = homozygosity - firstFreq - secondFreq + comboFreq;

    return res;
}
