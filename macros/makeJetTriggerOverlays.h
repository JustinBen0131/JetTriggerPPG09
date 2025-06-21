#ifndef MAKEJETTRIGGEROVERLAYS_H
#define MAKEJETTRIGGEROVERLAYS_H

#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <cctype>



// Define CutValues, FitParameters, and HistogramData within a dedicated namespace
namespace DataStructures {

    struct RunInfo {
        std::vector<int> runsBeforeFirmwareUpdate;
        std::vector<int> runsAfterFirmwareUpdate;
    };


    struct FitParameters {
        // Common parameters
        double amplitudeEstimate;
        double amplitudeMin;
        double amplitudeMax;

        // Sigmoid function parameters
        double slopeEstimate;
        double slopeMin;
        double slopeMax;
        double xOffsetEstimate;
        double xOffsetMin;
        double xOffsetMax;

        // Error function parameters
        double sigmaEstimate;
        double sigmaMin;
        double sigmaMax;
    };

} // namespace DataStructures


// ============================================================================
//  Trigger configuration
// ============================================================================
namespace TriggerConfig
{
    /* --------------------------------------------------------------------- */
    /* 1)  Trigger lists                                                     */
    /* --------------------------------------------------------------------- */

    /* every trigger you might meet in a file name                            */
    inline const std::vector<std::string> allTriggers = {
        "MBD_NandS_geq_1",
        "Jet_8_GeV_plus_MBD_NS_geq_1",
        "Jet_10_GeV_plus_MBD_NS_geq_1",
        "Jet_12_GeV_plus_MBD_NS_geq_1"
    };

    /* just the jet triggers (a handy subset)                                 */
    inline const std::vector<std::string> jetTriggers = {
        "Jet_8_GeV_plus_MBD_NS_geq_1",
        "Jet_10_GeV_plus_MBD_NS_geq_1",
        "Jet_12_GeV_plus_MBD_NS_geq_1"
    };

    /* --------------------------------------------------------------------- */
    /* 2)  Colours & pretty labels                                            */
    /* --------------------------------------------------------------------- */
    inline const std::map<std::string,int> triggerColorMap = {
        {"MBD_NandS_geq_1",                kBlack},
        {"Jet_8_GeV_plus_MBD_NS_geq_1",    kOrange - 3},
        {"Jet_10_GeV_plus_MBD_NS_geq_1",   kAzure  - 4},
        {"Jet_12_GeV_plus_MBD_NS_geq_1",   kPink   + 6}
    };

    /* internal-name → human-readable label                                   */
    inline const std::map<std::string,std::string> triggerNameMap = {
        {"MBD_NandS_geq_1",               "MBD NS #geq 1"},
        {"Jet_8_GeV_plus_MBD_NS_geq_1",   "Jet 8 GeV + MBD NS #geq 1"},
        {"Jet_10_GeV_plus_MBD_NS_geq_1",  "Jet 10 GeV + MBD NS #geq 1"},
        {"Jet_12_GeV_plus_MBD_NS_geq_1",  "Jet 12 GeV + MBD NS #geq 1"}
    };

    /* --------------------------------------------------------------------- */
    /* 3)  Per-combination fit seeds                                          */
    /*      key  =  ( full-combination-name , trigger-name )                 */
    /* --------------------------------------------------------------------- */
    using ComboKey = std::pair<std::string,std::string>;

    inline const std::map< ComboKey, DataStructures::FitParameters > kFitParameters = {

        /* ----------------------------------------------------------------- */
        /*  after-firmware-update  ––  three-jet combo                       */
        /* ----------------------------------------------------------------- */
        {
            { "MBD_NandS_geq_1_"
              "Jet_8_GeV_plus_MBD_NS_geq_1_"
              "Jet_10_GeV_plus_MBD_NS_geq_1_"
              "Jet_12_GeV_plus_MBD_NS_geq_1_afterTriggerFirmwareUpdate",
              "Jet_8_GeV_plus_MBD_NS_geq_1" },
            {/* Amp  */ 1.00, 0.999, 1.001,
             /* Slope*/ 0.52, 0.51, 0.53,
             /* X0   */ 10.0,  9.9, 10.1,
             /* Sigma*/ 0.5 , 0.1,  1.0 }
        },
        {
            { "MBD_NandS_geq_1_"
              "Jet_8_GeV_plus_MBD_NS_geq_1_"
              "Jet_10_GeV_plus_MBD_NS_geq_1_"
              "Jet_12_GeV_plus_MBD_NS_geq_1_afterTriggerFirmwareUpdate",
              "Jet_10_GeV_plus_MBD_NS_geq_1" },
            {/* Amp  */ 1.00, 0.999, 1.001,
             /* Slope*/ 0.52, 0.51, 0.53,
             /* X0   */ 12.0, 11.9, 12.1,
             /* Sigma*/ 0.5 , 0.1,  1.0 }
        },
        {
            { "MBD_NandS_geq_1_"
              "Jet_8_GeV_plus_MBD_NS_geq_1_"
              "Jet_10_GeV_plus_MBD_NS_geq_1_"
              "Jet_12_GeV_plus_MBD_NS_geq_1_afterTriggerFirmwareUpdate",
              "Jet_12_GeV_plus_MBD_NS_geq_1" },
            {/* Amp  */ 1.00, 0.999, 1.001,
             /* Slope*/ 0.52, 0.51, 0.53,
             /* X0   */ 14.3, 14.2, 14.4,
             /* Sigma*/ 0.5 , 0.1,  1.0 }
        },

        /* ----------------------------------------------------------------- */
        /*  before-firmware-update  ––  two-jet combo                        */
        /* ----------------------------------------------------------------- */
        {
            { "MBD_NandS_geq_1_"
              "Jet_8_GeV_plus_MBD_NS_geq_1_"
              "Jet_10_GeV_plus_MBD_NS_geq_1",
              "Jet_8_GeV_plus_MBD_NS_geq_1" },
            {/* Amp  */ 1.00, 0.999, 1.001,
             /* Slope*/ 0.52, 0.51, 0.53,
             /* X0   */ 10.0,  9.9, 10.1,
             /* Sigma*/ 0.5 , 0.1,  1.0 }
        },
        {
            { "MBD_NandS_geq_1_"
              "Jet_8_GeV_plus_MBD_NS_geq_1_"
              "Jet_10_GeV_plus_MBD_NS_geq_1",
              "Jet_10_GeV_plus_MBD_NS_geq_1" },
            {/* Amp  */ 1.00, 0.999, 1.001,
             /* Slope*/ 0.52, 0.51, 0.53,
             /* X0   */ 12.0, 11.9, 12.1,
             /* Sigma*/ 0.5 , 0.1,  1.0 }
        }
    };  // <-- don’t forget this semicolon
} // namespace TriggerConfig


namespace Utils {
    // Helper function to normalize the trigger combination string for case-insensitive and whitespace-insensitive comparison
    std::string normalizeString(const std::string& str) {
        std::string normalized = str;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
        normalized.erase(std::remove_if(normalized.begin(), normalized.end(), ::isspace), normalized.end());
        return normalized;
    }
    // Function to check if a string ends with another string
    bool EndsWith(const std::string& fullString, const std::string& ending) {
        if (fullString.length() >= ending.length()) {
            return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
        } else {
            return false;
        }
    }
    // Helper function to strip firmware update tags from the combination name
    std::string stripFirmwareTag(const std::string& combinationName) {
        std::string strippedName = combinationName;
        const std::vector<std::string> firmwareTags = {
            "_beforeTriggerFirmwareUpdate",
            "_afterTriggerFirmwareUpdate"
        };

        for (const auto& tag : firmwareTags) {
            size_t pos = strippedName.find(tag);
            if (pos != std::string::npos) {
                strippedName.erase(pos, tag.length());
                break;
            }
        }
        return strippedName;
    }

    std::string getTriggerCombinationName(const std::string& combinationName, const std::map<std::string, std::string>& nameMap) {
        std::string strippedCombinationName = stripFirmwareTag(combinationName);
        std::string normalizedCombinationName = normalizeString(strippedCombinationName);

        for (const auto& entry : nameMap) {
            if (normalizeString(entry.first) == normalizedCombinationName) {
                // Append firmware tag back to the human-readable name if present
                if (EndsWith(combinationName, "_beforeTriggerFirmwareUpdate")) {
                    return entry.second + " (Before Firmware Update)";
                } else if (EndsWith(combinationName, "_afterTriggerFirmwareUpdate")) {
                    return entry.second + " (After Firmware Update)";
                } else {
                    return entry.second;
                }
            }
        }
        return combinationName; // Default to combination name if not found
    }

    // Function to format a double to three significant figures as a string
    std::string formatToThreeSigFigs(double value) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(3) << value;
        return out.str();
    }

    // Existing sigmoidFit function remains unchanged
    TF1* sigmoidFit(const std::string& name, double xmin, double xmax,
                    double amplitude, double slope, double xOffset,
                    double amplitudeMin, double amplitudeMax,
                    double slopeMin, double slopeMax,
                    double xOffsetMin, double xOffsetMax) {
        // Define a sigmoid function for fitting
        TF1* fitFunc = new TF1(name.c_str(), "[0]/(1+exp(-[1]*(x-[2])))", xmin, xmax);
        fitFunc->SetParNames("Amplitude", "Slope", "XOffset");

        // Set initial parameters
        fitFunc->SetParameter(0, amplitude);  // Amplitude
        fitFunc->SetParameter(1, slope);      // Slope
        fitFunc->SetParameter(2, xOffset);    // XOffset

        // Set parameter limits
        fitFunc->SetParLimits(0, amplitudeMin, amplitudeMax);  // Amplitude limits
        fitFunc->SetParLimits(1, slopeMin, slopeMax);          // Slope limits
        fitFunc->SetParLimits(2, xOffsetMin, xOffsetMax);      // XOffset limits

        return fitFunc;
    }


    TF1* erfFit(const std::string& name, double xmin, double xmax,
                double amplitude, double xOffset, double sigma,
                double amplitudeMin, double amplitudeMax,
                double xOffsetMin, double xOffsetMax,
                double sigmaMin, double sigmaMax) {
        // Define an error function for fitting
        TF1* fitFunc = new TF1(name.c_str(), "[0]*0.5*(1+TMath::Erf((x-[1])/(sqrt(2)*[2])))", xmin, xmax);
        fitFunc->SetParNames("Amplitude", "XOffset", "Sigma");

        // Set initial parameters
        fitFunc->SetParameter(0, amplitude);  // Amplitude
        fitFunc->SetParameter(1, xOffset);    // XOffset
        fitFunc->SetParameter(2, sigma);      // Sigma

        // Set parameter limits
        fitFunc->SetParLimits(0, amplitudeMin, amplitudeMax);  // Amplitude limits
        fitFunc->SetParLimits(1, xOffsetMin, xOffsetMax);      // XOffset limits
        fitFunc->SetParLimits(2, sigmaMin, sigmaMax);          // Sigma limits

        return fitFunc;
    }

}

#endif // MAKEJETTRIGGEROVERLAYS_H
