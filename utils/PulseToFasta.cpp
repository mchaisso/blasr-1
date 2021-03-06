#include <string>
#include <iostream>
#include <vector>

#include <HDFPlsReader.hpp>
#include <HDFUtils.hpp>
#include <HDFRegionTableReader.hpp>
#include <reads/RegionTable.hpp>
#include <reads/ReadInterval.hpp>
#include <files/ReaderAgglomerate.hpp>
#include <utils/FileOfFileNames.hpp>
#include <utils/RegionUtils.hpp>
#include <utils/TimeUtils.hpp>
#include <SMRTSequence.hpp>
#include <utils.hpp>
#include <CommandLineParser.hpp>

char VERSION[] = "v1.0.0";
char PERFORCE_VERSION_STRING[] = "$Change: 126414 $";

int main(int argc, char* argv[]) {
    std::string program = "pls2fasta";
    std::string versionString = VERSION;
    AppendPerforceChangelist(PERFORCE_VERSION_STRING, versionString);

	std::string plsFileName, fastaOutName;
	std::vector<std::string> plsFileNames;
	bool trimByRegion, maskByRegion;
	trimByRegion = false;
	maskByRegion = false;
	RegionTable regionTable;
	std::string regionsFOFNName = "";
	std::vector<std::string> regionFileNames;
	bool splitSubreads = true;
	int minSubreadLength = 0;
	bool addSimulatedData = false;
  bool printFastq = false;
  bool printCcs   = false;
  int  lineLength = 50;
  int minReadScore = 0;
  std::vector<int> holeNumbers;
  CommandLineParser clp;
  bool printOnlyBest = false;

  clp.SetProgramName(program);
  clp.SetVersion(versionString);
  clp.RegisterStringOption("in.bax.h5", &plsFileName, "Input plx.h5/bax.h5/fofn file.", true);
  clp.RegisterStringOption("out.fasta", &fastaOutName, "Output fasta/fastq file.", true);
  clp.RegisterPreviousFlagsAsHidden();
  clp.RegisterFlagOption("trimByRegion", &trimByRegion, "Trim away low quality regions.");
  clp.RegisterFlagOption("maskByRegion", &maskByRegion, "Mask low quality regions with 'N'.");
  clp.RegisterStringOption("regionTable", &regionsFOFNName, "Optional HDF file with a /PulseData/Regions dataset.");
  clp.RegisterIntOption("minSubreadLength", &minSubreadLength, "Do not write subreads less than the specified length.", CommandLineParser::PositiveInteger);
  clp.RegisterFlagOption("noSplitSubreads", &splitSubreads, "Do not split reads on adapter sequences.");
  clp.RegisterIntListOption("holeNumber", &holeNumbers, "Only print this hole number (or list of numbers).");
  clp.RegisterFlagOption("fastq", &printFastq, "Print in FASTQ format with quality.");
  clp.RegisterFlagOption("ccs", &printCcs, "Print de novo CCS sequences");
  clp.RegisterIntOption("lineLength", &lineLength, "Specify fasta/fastq line length", CommandLineParser::PositiveInteger);
  clp.RegisterIntOption("minReadScore", &minReadScore, "Minimum read score to print a read.  The score is "
                        "a number between 0 and 1000 and represents the expected accuracy percentage * 10. "
                        "A typical value would be between 750 and 800.  This does not apply to ccs reads.", CommandLineParser::NonNegativeInteger);
  clp.RegisterFlagOption("best", &printOnlyBest, "If a CCS sequence exists, print this.  Otherwise, print the longest"
                         "subread.  This does not support fastq.");
  std::string description = ("Converts plx.h5/bax.h5/fofn files to fasta or fastq files. Although fasta files are provided"
  " with every run, they are not trimmed nor split into subreads. This program takes "
  "additional annotation information, such as the subread coordinates and high quality regions "
  "and uses them to create fasta sequences that are substrings of all bases called. Most of the time "
  "you will want to trim low quality reads, so you should specify -trimByRegion.");
  clp.SetProgramSummary(description);

  clp.ParseCommandLine(argc, argv);

    std::cerr << "[INFO] " << GetTimestamp() << " [" << program << "] started."  << std::endl;
	if (trimByRegion and maskByRegion) {
		std::cout << "ERROR! You cannot both trim and mask regions. Use one or the other." << std::endl;
		std::exit(EXIT_FAILURE);
	}

  if (printFastq) {
    // Setting lineLength to 0 flags to print on one line.
    lineLength = 0;
  }


    FileOfFileNames::StoreFileOrFileList(plsFileName, plsFileNames);
	if (regionsFOFNName == "") {
		regionFileNames = plsFileNames;
	}
	else {
        FileOfFileNames::StoreFileOrFileList(regionsFOFNName, regionFileNames);
	}

	std::ofstream fastaOut;
	CrucialOpen(fastaOutName, fastaOut);
	HDFRegionTableReader hdfRegionReader;
    sort(holeNumbers.begin(), holeNumbers.end());

    std::vector<int> pls2rgn = MapPls2Rgn(plsFileNames, regionFileNames);

    for (size_t plsFileIndex = 0; plsFileIndex < plsFileNames.size(); plsFileIndex++) {
        if (trimByRegion or maskByRegion or splitSubreads) {
            hdfRegionReader.Initialize(regionFileNames[pls2rgn[plsFileIndex]]);
            hdfRegionReader.ReadTable(regionTable);
        }

        ReaderAgglomerate reader;
        HDFBasReader ccsReader;

        if (printOnlyBest) {
            ccsReader.SetReadBasesFromCCS();
            ccsReader.Initialize(plsFileNames[plsFileIndex]);
        }
        if (printCcs == false) {
            reader.IgnoreCCS();
        }
        else {
            reader.hdfBasReader.SetReadBasesFromCCS();
        }
        if (addSimulatedData) {
            reader.hdfBasReader.IncludeField("SimulatedCoordinate");
            reader.hdfBasReader.IncludeField("SimulatedSequenceIndex");
        }

        if (reader.SetReadFileName(plsFileNames[plsFileIndex]) == 0) {
            std::cout << "ERROR, could not determine file type."
                << plsFileNames[plsFileIndex] << std::endl;
            std::exit(EXIT_FAILURE);
        }
        if (reader.Initialize() == 0) {
            std::cout << "ERROR, could not initialize file "
                << plsFileNames[plsFileIndex] << std::endl;
            std::exit(EXIT_FAILURE);
        }

        DNALength simulatedCoordinate;
        DNALength simulatedSequenceIndex;
        reader.SkipReadQuality();
        SMRTSequence seq;
        std::vector<ReadInterval> subreadIntervals;;
        SMRTSequence ccsSeq;
        while (reader.GetNextBases(seq, printFastq)) {
            if (printOnlyBest) {
                ccsReader.GetNext(ccsSeq);
            }

            if (holeNumbers.size() != 0 and
                    binary_search(holeNumbers.begin(), holeNumbers.end(), seq.zmwData.holeNumber) == false) {
                continue;
            }

            if (seq.length == 0) {
                continue;
            }

            if (addSimulatedData) {
                reader.hdfBasReader.simulatedCoordinateArray.Read(reader.hdfBasReader.curRead-1, reader.hdfBasReader.curRead, &simulatedCoordinate);
                reader.hdfBasReader.simulatedSequenceIndexArray.Read(reader.hdfBasReader.curRead-1, reader.hdfBasReader.curRead, &simulatedSequenceIndex);
            }

            if (printCcs == true) {
                if (printFastq == false) {
                    seq.PrintSeq(fastaOut);
                }
                else {
                    seq.PrintFastq(fastaOut, lineLength);
                }
                continue;
            }

            //
            // Determine the high quality boundaries of the read.  This is
            // the full read is no hq regions exist, or it is stated to
            // ignore regions.
            //
            DNALength hqReadStart, hqReadEnd;
            int hqRegionScore;
            if (GetReadTrimCoordinates(seq, seq.zmwData, regionTable, hqReadStart, hqReadEnd, hqRegionScore) == false or
                    (trimByRegion == false and maskByRegion == false)) {
                hqReadStart = 0;
                hqReadEnd   = seq.length;
            }

            //
            // Mask off the low quality portions of the reads.
            //
            if (maskByRegion) {
                if (hqReadStart > 0) {
                    std::fill(&seq.seq[0], &seq.seq[hqReadStart], 'N');
                }
                if (hqReadEnd != seq.length) {
                    std::fill(&seq.seq[hqReadEnd], &seq.seq[seq.length], 'N');
                }
            }



            //
            // Now possibly print the full read with masking.  This could be handled by making a
            //
            if (splitSubreads == false) {
                ReadInterval wholeRead(0, seq.length);
                // The set of subread intervals is just the entire read.
                subreadIntervals.clear();
                subreadIntervals.push_back(wholeRead);
            }
            else {
                //
                // Print subread coordinates no matter whether or not reads have subreads.
                //
                if (regionTable.HasHoleNumber(seq.HoleNumber())) {
                    subreadIntervals = regionTable[seq.HoleNumber()].SubreadIntervals(seq.length, false, true);
                } else {
                    subreadIntervals = {};
                }
            }
      //
      // Output all subreads as separate sequences.
      //
      SMRTSequence bestSubreadSequence;
      int bestSubreadScore = -1;
      int bestSubreadIndex = 0;
      SMRTSequence bestSubread;
      for (size_t intvIndex = 0; intvIndex < subreadIntervals.size(); intvIndex++) {
        SMRTSequence subreadSequence, subreadSequenceRC;

        subreadSequence.SubreadStart(subreadIntervals[intvIndex].start);
        subreadSequence.SubreadEnd  (subreadIntervals[intvIndex].end);

        //
        // When trimming by region, only output the parts of the
        // subread that overlap the hq region.
        //
        if (trimByRegion == true) {
          subreadSequence.SubreadStart(std::max((DNALength) subreadIntervals[intvIndex].start, hqReadStart));
          subreadSequence.SubreadEnd  (std::min((DNALength) subreadIntervals[intvIndex].end, hqReadEnd));
        }

        if (subreadSequence.SubreadStart() >= subreadSequence.SubreadEnd() or
            subreadSequence.SubreadEnd() - subreadSequence.SubreadStart() <= DNALength(minSubreadLength)) {
          //
          // There is no high quality portion of this subread. Skip it.
          //
          continue;
        }

        if (hqRegionScore < minReadScore) {
          continue;
        }

        //
        // Print the subread, adding the coordinates as part of the title.
        //
        subreadSequence.ReferenceSubstring(seq, subreadSequence.SubreadStart(),
                                           subreadSequence.SubreadLength());
        std::stringstream titleStream;
        titleStream << seq.title;
        if (splitSubreads) {
          //
          // Add the subread coordinates if splitting on subread.
          //
          titleStream << "/"
                      << subreadSequence.SubreadStart()
                      << "_" << subreadSequence.SubreadEnd();
        }

        //
        // If running on simulated data, add where the values were simulated from.
        //
        if (addSimulatedData) {
          titleStream << ((FASTASequence*)&seq)->title << "/chrIndex_"
                      << simulatedSequenceIndex << "/position_"<< simulatedCoordinate;
          ((FASTASequence*)&seq)->CopyTitle(titleStream.str());
        }

        subreadSequence.CopyTitle(titleStream.str());

        //
        // Eventually replace with WriterAgglomerate.
        //
        if (printOnlyBest == false) {
          if (subreadSequence.length > 0) {
            if (printFastq == false) {
              ((FASTASequence*)&subreadSequence)->PrintSeq(fastaOut);
            }
            else {
              subreadSequence.PrintFastq(fastaOut, lineLength);
            }
          }
        }
        else {
          int subreadWeightedScore = subreadSequence.length * hqRegionScore;
          if (subreadWeightedScore > bestSubreadScore) {
            bestSubreadIndex = intvIndex;
            (void)(bestSubreadIndex);
            bestSubread = subreadSequence;
            bestSubreadScore = subreadWeightedScore;
          }
        }
      }

      if (printOnlyBest) {
        if (ccsSeq.length > 0) {
          if (printFastq == false) {
            ccsSeq.PrintSeq(fastaOut);
          }
          else {
            ccsSeq.PrintFastq(fastaOut, ccsSeq.length);
          }
        }
        else {
          if (bestSubreadScore >= 0) {
            if (printFastq == false) {
              bestSubread.PrintSeq(fastaOut);
            }
            else {
              bestSubread.PrintFastq(fastaOut, bestSubread.length);
            }
            bestSubread.Free();
          }
        }
        ccsSeq.Free();
      }
      seq.Free();
    }
    reader.Close();
    hdfRegionReader.Close();
  }
  std::cerr << "[INFO] " << GetTimestamp() << " [" << program << "] ended."  << std::endl;
}
