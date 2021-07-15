//#include "RecoTauTag/RecoTau/interface/DeepTauBase.h"
#include "FWCore/Utilities/interface/isFinite.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "DataFormats/TauReco/interface/PFTauTransverseImpactParameterAssociation.h"
#include "DataFormats/PatCandidates/interface/PATTauDiscriminator.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "DataFormats/Common/interface/Handle.h"
#include "HeterogeneousCore/SonicTriton/interface/TritonEDProducer.h"

#include <Math/VectorUtil.h>
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/PatCandidates/interface/Electron.h"
#include "DataFormats/PatCandidates/interface/Muon.h"
#include "DataFormats/PatCandidates/interface/Tau.h"
#include "DataFormats/TauReco/interface/TauDiscriminatorContainer.h"
#include "DataFormats/TauReco/interface/PFTauDiscriminator.h"
#include "DataFormats/PatCandidates/interface/PATTauDiscriminator.h"
#include "CommonTools/Utils/interface/StringObjectFunction.h"
#include "RecoTauTag/RecoTau/interface/PFRecoTauClusterVariables.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "DataFormats/Common/interface/View.h"
#include "DataFormats/Common/interface/RefToBase.h"
#include "DataFormats/Provenance/interface/ProductProvenance.h"
#include "DataFormats/Provenance/interface/ProcessHistoryID.h"
#include "FWCore/Common/interface/Provenance.h"
#include <TF1.h>
#include <map>

#include "RecoTauTag/RecoTau/interface/DeepTauBase.h"

#include <fstream>
#include "tbb/concurrent_unordered_set.h"

#include <vector>

namespace deep_tau {
  constexpr int NumberOfOutputs = 4;
}

using namespace deep_tau_2017;

class DeepTauIdSonicProducer : public TritonEDProducer<> {
public:
  explicit DeepTauIdSonicProducer(edm::ParameterSet const& cfg)
      : TritonEDProducer<>(cfg, "DeepTauIdSonicProducer"),
        tausToken_(consumes<TauCollection>(cfg.getParameter<edm::InputTag>("taus"))),
        pfcandToken_(consumes<CandidateCollection>(cfg.getParameter<edm::InputTag>("pfcands"))),
        vtxToken_(consumes<reco::VertexCollection>(cfg.getParameter<edm::InputTag>("vertices"))),
        electrons_token_(consumes<std::vector<pat::Electron>>(cfg.getParameter<edm::InputTag>("electrons"))),
        muons_token_(consumes<std::vector<pat::Muon>>(cfg.getParameter<edm::InputTag>("muons"))),
        rho_token_(consumes<double>(cfg.getParameter<edm::InputTag>("rho"))),
        basicTauDiscriminators_inputToken_(consumes<reco::TauDiscriminatorContainer>(
            cfg.getUntrackedParameter<edm::InputTag>("basicTauDiscriminators"))),
        basicTauDiscriminatorsdR03_inputToken_(consumes<reco::TauDiscriminatorContainer>(
            cfg.getUntrackedParameter<edm::InputTag>("basicTauDiscriminatorsdR03"))),
        pfTauTransverseImpactParameters_token_(
            consumes<edm::AssociationVector<reco::PFTauRefProd, std::vector<reco::PFTauTransverseImpactParameterRef>>>(
                cfg.getParameter<edm::InputTag>("pfTauTransverseImpactParameters"))),
        version_(cfg.getParameter<unsigned>("version")),
        debug_level(cfg.getParameter<int>("debug_level")),
        disable_dxy_pca_(cfg.getParameter<bool>("disable_dxy_pca")),
        disable_hcalFraction_workaround_(cfg.getParameter<bool>("disable_hcalFraction_workaround")),
        disable_CellIndex_workaround_(cfg.getParameter<bool>("disable_CellIndex_workaround")),
        json_file_(nullptr),
        outputdiscs_(GetOutputDiscs()) {
    for (const auto& output_desc : outputdiscs_) {
      produces<TauDiscriminator>(output_desc.first);
      const auto& cut_list = cfg.getParameter<std::vector<std::string>>(output_desc.first + "WP");
      for (const std::string& cut_str : cut_list) {
        workingPoints_[output_desc.first].push_back(std::make_unique<Cutter>(cut_str));
      }
    }

    // prediscriminant operator
    // require the tau to pass the following prediscriminants
    const edm::ParameterSet& prediscriminantConfig = cfg.getParameter<edm::ParameterSet>("Prediscriminants");

    // determine boolean operator used on the prediscriminants
    std::string pdBoolOperator = prediscriminantConfig.getParameter<std::string>("BooleanOperator");
    // convert string to lowercase
    transform(pdBoolOperator.begin(), pdBoolOperator.end(), pdBoolOperator.begin(), ::tolower);

    if (pdBoolOperator == "and") {
      andPrediscriminants_ = 0x1;  //use chars instead of bools so we can do a bitwise trick later
    } else if (pdBoolOperator == "or") {
      andPrediscriminants_ = 0x0;
    } else {
      throw cms::Exception("TauDiscriminationProducerBase")
          << "PrediscriminantBooleanOperator defined incorrectly, options are: AND,OR";
    }

    // get the list of prediscriminants
    std::vector<std::string> prediscriminantsNames =
        prediscriminantConfig.getParameterNamesForType<edm::ParameterSet>();

    for (auto const& iDisc : prediscriminantsNames) {
      const edm::ParameterSet& iPredisc = prediscriminantConfig.getParameter<edm::ParameterSet>(iDisc);
      const edm::InputTag& label = iPredisc.getParameter<edm::InputTag>("Producer");
      double cut = iPredisc.getParameter<double>("cut");

      PATTauDiscInfo thisDiscriminator;
      thisDiscriminator.label = label;
      thisDiscriminator.cut = cut;
      thisDiscriminator.disc_token = consumes<pat::PATTauDiscriminator>(label);
      patPrediscriminants_.push_back(thisDiscriminator);
    }
  }

  using TauDiscriminator = deep_tau::DeepTauBase::TauDiscriminator;
  using TauCollection = deep_tau::DeepTauBase::TauCollection;
  using CandidateCollection = deep_tau::DeepTauBase::CandidateCollection;
  using TauRef = deep_tau::DeepTauBase::TauRef;
  using TauRefProd = deep_tau::DeepTauBase::TauRefProd;
  using ElectronCollection = deep_tau::DeepTauBase::ElectronCollection;
  using MuonCollection = deep_tau::DeepTauBase::MuonCollection;
  using Cutter = deep_tau::TauWPThreshold;
  using CutterPtr = deep_tau::DeepTauBase::CutterPtr;
  using WPList = deep_tau::DeepTauBase::WPList;
  using BasicDiscriminator = deep_tau::DeepTauBase::BasicDiscriminator;
  using PATTauDiscInfo = deep_tau::DeepTauBase::TauDiscInfo<pat::PATTauDiscriminator>;
  using OutputDisc = deep_tau::DeepTauBase::Output;

  void acquire(edm::Event const& iEvent, edm::EventSetup const& iSetup, Input& iInput) override;
  void produce(edm::Event& iEvent, edm::EventSetup const& iSetup, Output const& iOutput) override;
  void createOutputs(edm::Event& event, const std::vector<std::vector<float>>& pred, edm::Handle<TauCollection> taus);
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  float scale_and_rm_outlier(float val, float scale);

  using OutputDiscCollection = std::map<std::string, OutputDisc>;

  // select boolean operation on prediscriminants (and = 0x01, or = 0x00)
  uint8_t andPrediscriminants_;
  std::vector<PATTauDiscInfo> patPrediscriminants_;

  static const OutputDiscCollection& GetOutputDiscs() {
    static constexpr size_t e_index = 0, mu_index = 1, tau_index = 2, jet_index = 3;
    static const OutputDiscCollection outputdiscs_ = {
        {"VSe", OutputDisc({tau_index}, {e_index, tau_index})},
        {"VSmu", OutputDisc({tau_index}, {mu_index, tau_index})},
        {"VSjet", OutputDisc({tau_index}, {jet_index, tau_index})},
    };
    return outputdiscs_;
  }

private:
  edm::EDGetTokenT<TauCollection> tausToken_;
  edm::EDGetTokenT<CandidateCollection> pfcandToken_;
  edm::EDGetTokenT<reco::VertexCollection> vtxToken_;
  edm::EDGetTokenT<std::vector<pat::Electron>> electrons_token_;
  edm::EDGetTokenT<std::vector<pat::Muon>> muons_token_;
  edm::EDGetTokenT<double> rho_token_;
  edm::EDGetTokenT<reco::TauDiscriminatorContainer> basicTauDiscriminators_inputToken_;
  edm::EDGetTokenT<reco::TauDiscriminatorContainer> basicTauDiscriminatorsdR03_inputToken_;
  edm::EDGetTokenT<edm::AssociationVector<reco::PFTauRefProd, std::vector<reco::PFTauTransverseImpactParameterRef>>>
      pfTauTransverseImpactParameters_token_;
  std::string input_layer_, output_layer_;
  const unsigned version_;
  const int debug_level;
  const bool disable_dxy_pca_;
  const bool disable_hcalFraction_workaround_;
  const bool disable_CellIndex_workaround_;
  std::ofstream* json_file_;
  bool is_first_block_;

  OutputDiscCollection outputdiscs_;
  std::map<std::string, WPList> workingPoints_;

  std::vector<size_t> tau_indices_;

  static constexpr float pi = M_PI;
  static constexpr float default_value = -999.;

  template <typename T>
  static float getValue(T value) {
    return std::isnormal(value) ? static_cast<float>(value) : 0.f;
  }

  template <typename T>
  static float getValueLinear(T value, float min_value, float max_value, bool positive) {
    const float fixed_value = getValue(value);
    const float clamped_value = std::clamp(fixed_value, min_value, max_value);
    float transformed_value = (clamped_value - min_value) / (max_value - min_value);
    if (!positive)
      transformed_value = transformed_value * 2 - 1;
    return transformed_value;
  }

  template <typename T>
  static float getValueNorm(T value, float mean, float sigma, float n_sigmas_max = 5) {
    const float fixed_value = getValue(value);
    const float norm_value = (fixed_value - mean) / sigma;
    return std::clamp(norm_value, -n_sigmas_max, n_sigmas_max);
  }

  static bool isAbove(double value, double min) { return std::isnormal(value) && value > min; }

  static bool calculateElectronClusterVarsV2(const pat::Electron& ele,
                                             float& cc_ele_energy,
                                             float& cc_gamma_energy,
                                             int& cc_n_gamma) {
    cc_ele_energy = cc_gamma_energy = 0;
    cc_n_gamma = 0;
    const auto& superCluster = ele.superCluster();
    if (superCluster.isNonnull() && superCluster.isAvailable() && superCluster->clusters().isNonnull() &&
        superCluster->clusters().isAvailable()) {
      for (auto iter = superCluster->clustersBegin(); iter != superCluster->clustersEnd(); ++iter) {
        const float energy = static_cast<float>((*iter)->energy());
        if (iter == superCluster->clustersBegin())
          cc_ele_energy += energy;
        else {
          cc_gamma_energy += energy;
          ++cc_n_gamma;
        }
      }
      return true;
    } else
      return false;
  }

  //boolean to check if discriminator indices are already mapped
  bool discrIndicesMapped_ = false;
  std::map<BasicDiscriminator, size_t> basicDiscrIndexMap_;
  std::map<BasicDiscriminator, size_t> basicDiscrdR03IndexMap_;

  template <typename CandidateCastType, typename TauCastType>
  void getPredictionsV2(TauCollection::const_reference& tau,
                        const size_t tau_index,
                        const edm::RefToBase<reco::BaseTau> tau_ref,
                        const std::vector<pat::Electron>* electrons,
                        const std::vector<pat::Muon>* muons,
                        const edm::View<reco::Candidate>& pfCands,
                        const reco::Vertex& pv,
                        double rho,
                        TauFunc tau_funcs,
                        std::vector<float>& tauBlockInputs,
                        std::vector<float>& egammaInnerBlockInputs,
                        std::vector<float>& muonInnerBlockInputs,
                        std::vector<float>& hadronInnerBlockInputs,
                        std::vector<float>& egammaOuterBlockInputs,
                        std::vector<float>& muonOuterBlockInputs,
                        std::vector<float>& hadronOuterBlockInputs,
                        std::vector<int>& innerGridposInputs,
                        std::vector<int>& outerGridposInputs);

  template <typename Collection, typename TauCastType>
  void fillGrids(const TauCastType& tau, const Collection& objects, CellGrid& inner_grid, CellGrid& outer_grid);

  template <typename CandidateCastType, typename TauCastType>
  void createTauBlockInputs(const TauCastType& tau,
                            const size_t& tau_index,
                            const edm::RefToBase<reco::BaseTau> tau_ref,
                            const reco::Vertex& pv,
                            double rho,
                            TauFunc tau_funcs,
                            std::vector<float>& tauBlockInputs);

  template <typename CandidateCastType, typename TauCastType>
  void createEgammaBlockInputs(unsigned idx,
                               const TauCastType& tau,
                               const size_t tau_index,
                               const edm::RefToBase<reco::BaseTau> tau_ref,
                               const reco::Vertex& pv,
                               double rho,
                               const std::vector<pat::Electron>* electrons,
                               const edm::View<reco::Candidate>& pfCands,
                               const Cell& cell_map,
                               TauFunc tau_funcs,
                               bool is_inner,
                               std::vector<float>& egammaBlockInputs);

  template <typename CandidateCastType, typename TauCastType>
  void createMuonBlockInputs(unsigned idx,
                             const TauCastType& tau,
                             const size_t tau_index,
                             const edm::RefToBase<reco::BaseTau> tau_ref,
                             const reco::Vertex& pv,
                             double rho,
                             const std::vector<pat::Muon>* muons,
                             const edm::View<reco::Candidate>& pfCands,
                             const Cell& cell_map,
                             TauFunc tau_funcs,
                             bool is_inner,
                             std::vector<float>& muonBlockInputs);

  template <typename CandidateCastType, typename TauCastType>
  void createHadronsBlockInputs(unsigned idx,
                                const TauCastType& tau,
                                const size_t tau_index,
                                const edm::RefToBase<reco::BaseTau> tau_ref,
                                const reco::Vertex& pv,
                                double rho,
                                const edm::View<reco::Candidate>& pfCands,
                                const Cell& cell_map,
                                TauFunc tau_funcs,
                                bool is_inner,
                                std::vector<float>& hadronBlockInputs);

  template <typename CandidateCastType, typename TauCastType>
  void createConvFeatures(const TauCastType& tau,
                          const size_t tau_index,
                          const edm::RefToBase<reco::BaseTau> tau_ref,
                          const reco::Vertex& pv,
                          double rho,
                          const std::vector<pat::Electron>* electrons,
                          const std::vector<pat::Muon>* muons,
                          const edm::View<reco::Candidate>& pfCands,
                          const CellGrid& grid,
                          TauFunc tau_funcs,
                          bool is_inner,
                          std::vector<float>& egammaBlockInputs,
                          std::vector<float>& muonBlockInputs,
                          std::vector<float>& hadronBlockInputs,
                          std::vector<int>& GridposInputs);

  static double getInnerSignalConeRadius(double pt) {
    static constexpr double min_pt = 30., min_radius = 0.05, cone_opening_coef = 3.;
    // This is equivalent of the original formula (std::max(std::min(0.1, 3.0/pt), 0.05)
    return std::max(cone_opening_coef / std::max(pt, min_pt), min_radius);
  }

  // Copied from https://github.com/cms-sw/cmssw/blob/CMSSW_9_4_X/RecoTauTag/RecoTau/plugins/PATTauDiscriminationByMVAIsolationRun2.cc#L218
  template <typename TauCastType>
  static bool calculateGottfriedJacksonAngleDifference(const TauCastType& tau,
                                                       const size_t tau_index,
                                                       double& gj_diff,
                                                       TauFunc tau_funcs) {
    if (tau_funcs.getHasSecondaryVertex(tau, tau_index)) {
      static constexpr double mTau = 1.77682;
      const double mAOne = tau.p4().M();
      const double pAOneMag = tau.p();
      const double argumentThetaGJmax = (std::pow(mTau, 2) - std::pow(mAOne, 2)) / (2 * mTau * pAOneMag);
      const double argumentThetaGJmeasured = tau.p4().Vect().Dot(tau_funcs.getFlightLength(tau, tau_index)) /
                                             (pAOneMag * tau_funcs.getFlightLength(tau, tau_index).R());
      if (std::abs(argumentThetaGJmax) <= 1. && std::abs(argumentThetaGJmeasured) <= 1.) {
        double thetaGJmax = std::asin(argumentThetaGJmax);
        double thetaGJmeasured = std::acos(argumentThetaGJmeasured);
        gj_diff = thetaGJmeasured - thetaGJmax;
        return true;
      }
    }
    return false;
  }

  template <typename TauCastType>
  static float calculateGottfriedJacksonAngleDifference(const TauCastType& tau,
                                                        const size_t tau_index,
                                                        TauFunc tau_funcs) {
    double gj_diff;
    if (calculateGottfriedJacksonAngleDifference(tau, tau_index, gj_diff, tau_funcs))
      return static_cast<float>(gj_diff);
    return default_value;
  }

  static bool isInEcalCrack(double eta) {
    const double abs_eta = std::abs(eta);
    return abs_eta > 1.46 && abs_eta < 1.558;
  }
};

void DeepTauIdSonicProducer::acquire(edm::Event const& iEvent, edm::EventSetup const& iSetup, Input& iInput) {
  edm::Handle<TauCollection> taus;
  iEvent.getByToken(tausToken_, taus);

  edm::ProductID tauProductID = taus.id();

  // load prediscriminators
  size_t nPrediscriminants = patPrediscriminants_.size();
  for (size_t iDisc = 0; iDisc < nPrediscriminants; ++iDisc) {
    edm::ProductID discKeyId;
    patPrediscriminants_[iDisc].fill(iEvent);
    discKeyId = patPrediscriminants_[iDisc].handle->keyProduct().id();

    // Check to make sure the product is correct for the discriminator.
    // If not, throw a more informative exception.
    if (tauProductID != discKeyId) {
      throw cms::Exception("MisconfiguredPrediscriminant")
          << "The tau collection has product ID: " << tauProductID
          << " but the pre-discriminator is keyed with product ID: " << discKeyId << std::endl;
    }
  }

  const reco::TauDiscriminatorContainer basicTauDiscriminators_default;
  const reco::TauDiscriminatorContainer basicTauDiscriminatorsdR03_default;
  const edm::AssociationVector<reco::PFTauRefProd, std::vector<reco::PFTauTransverseImpactParameterRef>>
      pfTauTransverseImpactParameters_default;

  const std::vector<pat::Electron>* electron_collection;
  const std::vector<pat::Muon>* muon_collection;
  const reco::TauDiscriminatorContainer* basicTauDiscriminators;
  const reco::TauDiscriminatorContainer* basicTauDiscriminatorsdR03;
  const edm::AssociationVector<reco::PFTauRefProd, std::vector<reco::PFTauTransverseImpactParameterRef>>*
      pfTauTransverseImpactParameters;

  electron_collection = &iEvent.get(electrons_token_);
  muon_collection = &iEvent.get(muons_token_);
  pfTauTransverseImpactParameters = &pfTauTransverseImpactParameters_default;
  basicTauDiscriminators = &basicTauDiscriminators_default;
  basicTauDiscriminatorsdR03 = &basicTauDiscriminatorsdR03_default;

  TauFunc tauIDs = {basicTauDiscriminators,
                    basicTauDiscriminatorsdR03,
                    pfTauTransverseImpactParameters,
                    basicDiscrIndexMap_,
                    basicDiscrdR03IndexMap_};

  edm::Handle<edm::View<reco::Candidate>> pfCands;
  iEvent.getByToken(pfcandToken_, pfCands);

  edm::Handle<reco::VertexCollection> vertices;
  iEvent.getByToken(vtxToken_, vertices);

  edm::Handle<double> rho;
  iEvent.getByToken(rho_token_, rho);

  // vector to store the indices for the taus passing the selections
  tau_indices_.clear();

  for (size_t tau_index = 0; tau_index < taus->size(); ++tau_index) {
    const edm::RefToBase<reco::BaseTau> tauRef = taus->refAt(tau_index);
    bool passesPrediscriminants = true;
    passesPrediscriminants =
        tauIDs.passPrediscriminants<std::vector<PATTauDiscInfo>>(patPrediscriminants_, andPrediscriminants_, tauRef);
    if (!passesPrediscriminants)
      continue;

    // tau index that passes the selection
    tau_indices_.push_back(tau_index);
  }

  if (tau_indices_.empty()) {
    // no tau passing the requirement
    // no need to run acquire and inference
    client_->setBatchSize(0);
    return;
  }

  // always set the batch size to 1, since the 'batch' for
  // deeptau is different with the traditional ones
  client_->setBatchSize(1);
  int n_taus = tau_indices_.size();

  // tau block
  auto& input_tauBlock = iInput.at("input_tau");
  input_tauBlock.setShape(0, n_taus);
  auto data_tauBlock = input_tauBlock.allocate<float>();
  auto& vdata_tauBlock = (*data_tauBlock)[0];

  // for inner and outer grids
  // usually less than 10 inner grids and 50 outer grids per tau
  // set these numbers temporarily for vector reservation
  int n_inner_cells = 10 * n_taus;
  int n_outer_cells = 50 * n_taus;

  auto& input_innerEgammaBlock = iInput.at("input_inner_egamma");
  input_innerEgammaBlock.setShape(0, n_inner_cells);
  auto data_innerEgammaBlock = input_innerEgammaBlock.allocate<float>();
  auto& vdata_innerEgammaBlock = (*data_innerEgammaBlock)[0];

  auto& input_outerEgammaBlock = iInput.at("input_outer_egamma");
  input_outerEgammaBlock.setShape(0, n_outer_cells);
  auto data_outerEgammaBlock = input_outerEgammaBlock.allocate<float>();
  auto& vdata_outerEgammaBlock = (*data_outerEgammaBlock)[0];

  // muonTensor for inner and outer
  auto& input_innerMuonBlock = iInput.at("input_inner_muon");
  input_innerMuonBlock.setShape(0, n_inner_cells);
  auto data_innerMuonBlock = input_innerMuonBlock.allocate<float>();
  auto& vdata_innerMuonBlock = (*data_innerMuonBlock)[0];

  auto& input_outerMuonBlock = iInput.at("input_outer_muon");
  input_outerMuonBlock.setShape(0, n_outer_cells);
  auto data_outerMuonBlock = input_outerMuonBlock.allocate<float>();
  auto& vdata_outerMuonBlock = (*data_outerMuonBlock)[0];

  // hadronTensor for inner and outer
  auto& input_innerHadronBlock = iInput.at("input_inner_hadrons");
  input_innerHadronBlock.setShape(0, n_inner_cells);
  auto data_innerHadronBlock = input_innerHadronBlock.allocate<float>();
  auto& vdata_innerHadronBlock = (*data_innerHadronBlock)[0];

  auto& input_outerHadronBlock = iInput.at("input_outer_hadrons");
  input_outerHadronBlock.setShape(0, n_outer_cells);
  auto data_outerHadronBlock = input_outerHadronBlock.allocate<float>();
  auto& vdata_outerHadronBlock = (*data_outerHadronBlock)[0];

  // coordinates of the inner grids: n_inner_cells x 3 (i_tau, j_eta, k_phi)
  auto& input_innerGridposBlock = iInput.at("input_inner_pos");
  input_innerGridposBlock.setShape(0, n_inner_cells);
  auto data_innerGridposBlock = input_innerGridposBlock.allocate<int64_t>();
  auto& vdata_innerGridposBlock = (*data_innerGridposBlock)[0];

  // coordinates of the outer grids: n_outer_cells x 3 (i_tau, j_eta, k_phi)
  auto& input_outerGridposBlock = iInput.at("input_outer_pos");
  input_outerGridposBlock.setShape(0, n_outer_cells);
  auto data_outerGridposBlock = input_outerGridposBlock.allocate<int64_t>();
  auto& vdata_outerGridposBlock = (*data_outerGridposBlock)[0];

  for (int tau_index : tau_indices_) {
    std::vector<float> tauBlock;

    // inner grid
    std::vector<float> egammaInnerBlock;
    std::vector<float> muonInnerBlock;
    std::vector<float> hadronInnerBlock;

    // outer grid
    std::vector<float> egammaOuterBlock;
    std::vector<float> muonOuterBlock;
    std::vector<float> hadronOuterBlock;

    // pos
    std::vector<int> innerGridposBlock;
    std::vector<int> outerGridposBlock;

    const edm::RefToBase<reco::BaseTau> tauRef = taus->refAt(tau_index);
    getPredictionsV2<pat::PackedCandidate, pat::Tau>(taus->at(tau_index),
                                                     tau_index,
                                                     tauRef,
                                                     electron_collection,
                                                     muon_collection,
                                                     *pfCands,
                                                     vertices->at(0),
                                                     *rho,
                                                     tauIDs,
                                                     tauBlock,
                                                     egammaInnerBlock,
                                                     muonInnerBlock,
                                                     hadronInnerBlock,
                                                     egammaOuterBlock,
                                                     muonOuterBlock,
                                                     hadronOuterBlock,
                                                     innerGridposBlock,
                                                     outerGridposBlock);

    vdata_tauBlock.insert(vdata_tauBlock.end(), tauBlock.begin(), tauBlock.end());

    vdata_innerEgammaBlock.insert(vdata_innerEgammaBlock.end(), egammaInnerBlock.begin(), egammaInnerBlock.end());
    vdata_innerMuonBlock.insert(vdata_innerMuonBlock.end(), muonInnerBlock.begin(), muonInnerBlock.end());
    vdata_innerHadronBlock.insert(vdata_innerHadronBlock.end(), hadronInnerBlock.begin(), hadronInnerBlock.end());

    vdata_outerEgammaBlock.insert(vdata_outerEgammaBlock.end(), egammaOuterBlock.begin(), egammaOuterBlock.end());
    vdata_outerMuonBlock.insert(vdata_outerMuonBlock.end(), muonOuterBlock.begin(), muonOuterBlock.end());
    vdata_outerHadronBlock.insert(vdata_outerHadronBlock.end(), hadronOuterBlock.begin(), hadronOuterBlock.end());

    // map to save the inner/outer grid position and the associated tau indices in one event
    // used for the core network
    vdata_innerGridposBlock.insert(vdata_innerGridposBlock.end(), innerGridposBlock.begin(), innerGridposBlock.end());
    vdata_outerGridposBlock.insert(vdata_outerGridposBlock.end(), outerGridposBlock.begin(), outerGridposBlock.end());
  }

  // insert one collection of zeros to calculate the 'ZeroOutputTensor'
  // i.e., the output from inner/outer network when the input is zero
  // this tensor will be used to pad the core network for the cells without any particle
  vdata_innerEgammaBlock.insert(
      vdata_innerEgammaBlock.end(), dnn_inputs_2017_v2::EgammaBlockInputs::NumberOfInputs, 0.);
  vdata_innerMuonBlock.insert(vdata_innerMuonBlock.end(), dnn_inputs_2017_v2::MuonBlockInputs::NumberOfInputs, 0.);
  vdata_innerHadronBlock.insert(
      vdata_innerHadronBlock.end(), dnn_inputs_2017_v2::HadronBlockInputs::NumberOfInputs, 0.);

  vdata_outerEgammaBlock.insert(
      vdata_outerEgammaBlock.end(), dnn_inputs_2017_v2::EgammaBlockInputs::NumberOfInputs, 0.);
  vdata_outerMuonBlock.insert(vdata_outerMuonBlock.end(), dnn_inputs_2017_v2::MuonBlockInputs::NumberOfInputs, 0.);
  vdata_outerHadronBlock.insert(
      vdata_outerHadronBlock.end(), dnn_inputs_2017_v2::HadronBlockInputs::NumberOfInputs, 0.);

  // tau
  input_tauBlock.toServer(data_tauBlock);

  // inner
  // the actual number of inner cells in the event + 1
  // Note the last element of the Egamma, Muon, and Hadron Block is the zero-paddled vector
  // for retriving outputs from the inner network when the inputs are zero, which will be
  // used to paddle the inputs for the core network
  n_inner_cells = (vdata_innerEgammaBlock.size() / dnn_inputs_2017_v2::EgammaBlockInputs::NumberOfInputs);
  input_innerEgammaBlock.setShape(0, n_inner_cells);
  input_innerEgammaBlock.toServer(data_innerEgammaBlock);
  input_innerMuonBlock.setShape(0, n_inner_cells);
  input_innerMuonBlock.toServer(data_innerMuonBlock);
  input_innerHadronBlock.setShape(0, n_inner_cells);
  input_innerHadronBlock.toServer(data_innerHadronBlock);

  // outer
  // the actual number of outer cells in the event + 1
  // Note the last element of the Egamma, Muon, and Hadron Block is the zero-paddled vector
  // for retriving outputs from the outer network when the inputs are zero, which will be
  // used to paddle the inputs for the core network
  n_outer_cells = (vdata_outerEgammaBlock.size() / dnn_inputs_2017_v2::EgammaBlockInputs::NumberOfInputs);
  input_outerEgammaBlock.setShape(0, n_outer_cells);
  input_outerEgammaBlock.toServer(data_outerEgammaBlock);
  input_outerMuonBlock.setShape(0, n_outer_cells);
  input_outerMuonBlock.toServer(data_outerMuonBlock);
  input_outerHadronBlock.setShape(0, n_outer_cells);
  input_outerHadronBlock.toServer(data_outerHadronBlock);

  // grid coordinates (i-th tau, j-th eta, k-th phi) of the inner and outer cells
  // The last element from the inner and outer network is zero-paddled vector
  // subtract it when setting the Gridpos shape
  input_innerGridposBlock.setShape(0, n_inner_cells - 1);
  input_innerGridposBlock.toServer(data_innerGridposBlock);
  input_outerGridposBlock.setShape(0, n_outer_cells - 1);
  input_outerGridposBlock.toServer(data_outerGridposBlock);
}

void DeepTauIdSonicProducer::produce(edm::Event& iEvent, edm::EventSetup const& iSetup, Output const& iOutput) {
  if (tau_indices_.empty()) {
    std::cout << "no tau sent to the server; skip this event in produce" << std::endl;
    return;
  }
  edm::Handle<TauCollection> taus;
  iEvent.getByToken(tausToken_, taus);
  const auto& output_tauval = iOutput.at("main_output/Softmax");
  // the current mode always runs with batchSize of 1
  const auto& outputs_tauval = output_tauval.fromServer<float>();

  // fill the taus passing the selections with the results from produce,
  //  and the taus failing the selections with zero
  std::vector<std::vector<float>> pred_all(taus->size(), std::vector<float>(deep_tau::NumberOfOutputs, 0.));
  for (unsigned itau_passed = 0; itau_passed < tau_indices_.size(); ++itau_passed) {
    int tau_index = tau_indices_[itau_passed];
    std::copy(outputs_tauval[0].begin() + deep_tau::NumberOfOutputs * itau_passed,
              outputs_tauval[0].begin() + deep_tau::NumberOfOutputs * (itau_passed + 1),
              pred_all[tau_index].begin());
  }

  createOutputs(iEvent, pred_all, taus);
}

void DeepTauIdSonicProducer::createOutputs(edm::Event& event,
                                           const std::vector<std::vector<float>>& pred,
                                           edm::Handle<TauCollection> taus) {
  for (const auto& output_desc : outputdiscs_) {
    const WPList* working_points = nullptr;
    if (workingPoints_.find(output_desc.first) != workingPoints_.end()) {
      working_points = &workingPoints_.at(output_desc.first);
    }
    auto result = output_desc.second.get_value(taus, pred, working_points, false);
    event.put(std::move(result), output_desc.first);
  }
}

template <typename CandidateCastType, typename TauCastType>
void DeepTauIdSonicProducer::getPredictionsV2(TauCollection::const_reference& tau,
                                              const size_t tau_index,
                                              const edm::RefToBase<reco::BaseTau> tau_ref,
                                              const std::vector<pat::Electron>* electrons,
                                              const std::vector<pat::Muon>* muons,
                                              const edm::View<reco::Candidate>& pfCands,
                                              const reco::Vertex& pv,
                                              double rho,
                                              TauFunc tau_funcs,
                                              std::vector<float>& tauBlockInputs,
                                              std::vector<float>& egammaInnerBlockInputs,
                                              std::vector<float>& muonInnerBlockInputs,
                                              std::vector<float>& hadronInnerBlockInputs,
                                              std::vector<float>& egammaOuterBlockInputs,
                                              std::vector<float>& muonOuterBlockInputs,
                                              std::vector<float>& hadronOuterBlockInputs,
                                              std::vector<int>& innerGridposInputs,
                                              std::vector<int>& outerGridposInputs) {
  CellGrid inner_grid(dnn_inputs_2017_v2::number_of_inner_cell,
                      dnn_inputs_2017_v2::number_of_inner_cell,
                      0.02,
                      0.02,
                      disable_CellIndex_workaround_);
  CellGrid outer_grid(dnn_inputs_2017_v2::number_of_outer_cell,
                      dnn_inputs_2017_v2::number_of_outer_cell,
                      0.05,
                      0.05,
                      disable_CellIndex_workaround_);
  // fill in the inner and outer grids for electrons, muons, and pfCands
  fillGrids(dynamic_cast<const TauCastType&>(tau), *electrons, inner_grid, outer_grid);
  fillGrids(dynamic_cast<const TauCastType&>(tau), *muons, inner_grid, outer_grid);
  fillGrids(dynamic_cast<const TauCastType&>(tau), pfCands, inner_grid, outer_grid);

  tauBlockInputs.resize(dnn_inputs_2017_v2::TauBlockInputs::NumberOfInputs, 0.);
  createTauBlockInputs<CandidateCastType>(
      dynamic_cast<const TauCastType&>(tau), tau_index, tau_ref, pv, rho, tau_funcs, tauBlockInputs);
  using namespace dnn_inputs_2017_v2;

  // egamma, muon, and hadron inner and outer inputs for the grids
  createConvFeatures<CandidateCastType>(dynamic_cast<const TauCastType&>(tau),
                                        tau_index,
                                        tau_ref,
                                        pv,
                                        rho,
                                        electrons,
                                        muons,
                                        pfCands,
                                        inner_grid,
                                        tau_funcs,
                                        true,
                                        egammaInnerBlockInputs,
                                        muonInnerBlockInputs,
                                        hadronInnerBlockInputs,
                                        innerGridposInputs);
  createConvFeatures<CandidateCastType>(dynamic_cast<const TauCastType&>(tau),
                                        tau_index,
                                        tau_ref,
                                        pv,
                                        rho,
                                        electrons,
                                        muons,
                                        pfCands,
                                        outer_grid,
                                        tau_funcs,
                                        false,
                                        egammaOuterBlockInputs,
                                        muonOuterBlockInputs,
                                        hadronOuterBlockInputs,
                                        outerGridposInputs);
}

template <typename Collection, typename TauCastType>
void DeepTauIdSonicProducer::fillGrids(const TauCastType& tau,
                                       const Collection& objects,
                                       CellGrid& inner_grid,
                                       CellGrid& outer_grid) {
  static constexpr double outer_dR2 = 0.25;  //0.5^2
  const double inner_radius = getInnerSignalConeRadius(tau.polarP4().pt());
  const double inner_dR2 = std::pow(inner_radius, 2);

  const auto addObject = [&](size_t n, double deta, double dphi, CellGrid& grid) {
    const auto& obj = objects.at(n);
    const CellObjectType obj_type = GetCellObjectType(obj);
    if (obj_type == CellObjectType::Other)
      return;
    CellIndex cell_index;
    if (grid.tryGetCellIndex(deta, dphi, cell_index)) {
      Cell& cell = grid[cell_index];
      auto iter = cell.find(obj_type);
      if (iter != cell.end()) {
        const auto& prev_obj = objects.at(iter->second);
        // fill in the grid with the particle of highest-pT
        if (obj.polarP4().pt() > prev_obj.polarP4().pt())
          iter->second = n;
      } else {
        cell[obj_type] = n;
      }
    }
  };

  for (size_t n = 0; n < objects.size(); ++n) {
    const auto& obj = objects.at(n);
    const double deta = obj.polarP4().eta() - tau.polarP4().eta();
    const double dphi = reco::deltaPhi(obj.polarP4().phi(), tau.polarP4().phi());
    const double dR2 = std::pow(deta, 2) + std::pow(dphi, 2);
    if (dR2 < inner_dR2)
      addObject(n, deta, dphi, inner_grid);
    if (dR2 < outer_dR2)
      addObject(n, deta, dphi, outer_grid);
  }
}

template <typename CandidateCastType, typename TauCastType>
void DeepTauIdSonicProducer::createConvFeatures(const TauCastType& tau,
                                                const size_t tau_index,
                                                const edm::RefToBase<reco::BaseTau> tau_ref,
                                                const reco::Vertex& pv,
                                                double rho,
                                                const std::vector<pat::Electron>* electrons,
                                                const std::vector<pat::Muon>* muons,
                                                const edm::View<reco::Candidate>& pfCands,
                                                const CellGrid& grid,
                                                TauFunc tau_funcs,
                                                bool is_inner,
                                                std::vector<float>& egammaBlockInputs,
                                                std::vector<float>& muonBlockInputs,
                                                std::vector<float>& hadronBlockInputs,
                                                std::vector<int>& GridposInputs) {
  // fill in the block inputs with zeros
  int n_cells = grid.num_valid_cells();

  egammaBlockInputs.resize(n_cells * dnn_inputs_2017_v2::EgammaBlockInputs::NumberOfInputs, 0.);
  muonBlockInputs.resize(n_cells * dnn_inputs_2017_v2::MuonBlockInputs::NumberOfInputs, 0.);
  hadronBlockInputs.resize(n_cells * dnn_inputs_2017_v2::HadronBlockInputs::NumberOfInputs, 0.);

  unsigned idx = 0;
  for (int eta = -grid.maxEtaIndex(); eta <= grid.maxEtaIndex(); ++eta) {
    for (int phi = -grid.maxPhiIndex(); phi <= grid.maxPhiIndex(); ++phi) {
      if (debug_level >= 2) {
        std::cout << "processing ( eta = " << eta << ", phi = " << phi << " )" << std::endl;
      }
      const CellIndex cell_index{eta, phi};
      const int eta_index = grid.getEtaTensorIndex(cell_index);
      const int phi_index = grid.getPhiTensorIndex(cell_index);

      const auto cell_iter = grid.find(cell_index);
      if (cell_iter != grid.end()) {
        if (debug_level >= 2) {
          std::cout << " creating inputs for ( eta = " << eta << ", phi = " << phi << " ): idx = " << idx << std::endl;
        }
        const Cell& cell = cell_iter->second;
        createEgammaBlockInputs<CandidateCastType>(
            idx, tau, tau_index, tau_ref, pv, rho, electrons, pfCands, cell, tau_funcs, is_inner, egammaBlockInputs);
        createMuonBlockInputs<CandidateCastType>(
            idx, tau, tau_index, tau_ref, pv, rho, muons, pfCands, cell, tau_funcs, is_inner, muonBlockInputs);
        createHadronsBlockInputs<CandidateCastType>(
            idx, tau, tau_index, tau_ref, pv, rho, pfCands, cell, tau_funcs, is_inner, hadronBlockInputs);

        GridposInputs.push_back(tau_index);
        GridposInputs.push_back(eta_index);
        GridposInputs.push_back(phi_index);
        idx += 1;
      } else {
        if (debug_level >= 2) {
          std::cout << " skipping creation of inputs, because ( eta = " << eta << ", phi = " << phi
                    << " ) is not in the grid !!" << std::endl;
        }
      }
    }
  }
}

template <typename CandidateCastType, typename TauCastType>
void DeepTauIdSonicProducer::createTauBlockInputs(const TauCastType& tau,
                                                  const size_t& tau_index,
                                                  const edm::RefToBase<reco::BaseTau> tau_ref,
                                                  const reco::Vertex& pv,
                                                  double rho,
                                                  TauFunc tau_funcs,
                                                  std::vector<float>& tauBlockInputs) {
  namespace dnn = dnn_inputs_2017_v2::TauBlockInputs;
  const auto& get = [&](int var_index) -> float& { return tauBlockInputs.at(var_index); };

  auto leadChargedHadrCand = dynamic_cast<const CandidateCastType*>(tau.leadChargedHadrCand().get());

  get(dnn::rho) = getValueNorm(rho, 21.49f, 9.713f);
  get(dnn::tau_pt) = getValueLinear(tau.polarP4().pt(), 20.f, 1000.f, true);
  get(dnn::tau_eta) = getValueLinear(tau.polarP4().eta(), -2.3f, 2.3f, false);
  get(dnn::tau_phi) = getValueLinear(tau.polarP4().phi(), -pi, pi, false);
  get(dnn::tau_mass) = getValueNorm(tau.polarP4().mass(), 0.6669f, 0.6553f);
  get(dnn::tau_E_over_pt) = getValueLinear(tau.p4().energy() / tau.p4().pt(), 1.f, 5.2f, true);
  get(dnn::tau_charge) = getValue(tau.charge());
  get(dnn::tau_n_charged_prongs) = getValueLinear(tau.decayMode() / 5 + 1, 1, 3, true);
  get(dnn::tau_n_neutral_prongs) = getValueLinear(tau.decayMode() % 5, 0, 2, true);
  get(dnn::chargedIsoPtSum) = getValueNorm(tau_funcs.getChargedIsoPtSum(tau, tau_ref), 47.78f, 123.5f);
  get(dnn::chargedIsoPtSumdR03_over_dR05) =
      getValue(tau_funcs.getChargedIsoPtSumdR03(tau, tau_ref) / tau_funcs.getChargedIsoPtSum(tau, tau_ref));
  get(dnn::footprintCorrection) = getValueNorm(tau_funcs.getFootprintCorrectiondR03(tau, tau_ref), 9.029f, 26.42f);
  get(dnn::neutralIsoPtSum) = getValueNorm(tau_funcs.getNeutralIsoPtSum(tau, tau_ref), 57.59f, 155.3f);
  get(dnn::neutralIsoPtSumWeight_over_neutralIsoPtSum) =
      getValue(tau_funcs.getNeutralIsoPtSumWeight(tau, tau_ref) / tau_funcs.getNeutralIsoPtSum(tau, tau_ref));
  get(dnn::neutralIsoPtSumWeightdR03_over_neutralIsoPtSum) =
      getValue(tau_funcs.getNeutralIsoPtSumdR03Weight(tau, tau_ref) / tau_funcs.getNeutralIsoPtSum(tau, tau_ref));
  get(dnn::neutralIsoPtSumdR03_over_dR05) =
      getValue(tau_funcs.getNeutralIsoPtSumdR03(tau, tau_ref) / tau_funcs.getNeutralIsoPtSum(tau, tau_ref));
  get(dnn::photonPtSumOutsideSignalCone) =
      getValueNorm(tau_funcs.getPhotonPtSumOutsideSignalCone(tau, tau_ref), 1.731f, 6.846f);
  get(dnn::puCorrPtSum) = getValueNorm(tau_funcs.getPuCorrPtSum(tau, tau_ref), 22.38f, 16.34f);
  // The global PCA coordinates were used as inputs during the NN training, but it was decided to disable
  // them for the inference, because modeling of dxy_PCA in MC poorly describes the data, and x and y coordinates
  // in data results outside of the expected 5 std. dev. input validity range. On the other hand,
  // these coordinates are strongly era-dependent. Kept as comment to document what NN expects.
  if (!disable_dxy_pca_) {
    auto const pca = tau_funcs.getdxyPCA(tau, tau_index);
    get(dnn::tau_dxy_pca_x) = getValueNorm(pca.x(), -0.0241f, 0.0074f);
    get(dnn::tau_dxy_pca_y) = getValueNorm(pca.y(), 0.0675f, 0.0128f);
    get(dnn::tau_dxy_pca_z) = getValueNorm(pca.z(), 0.7973f, 3.456f);
  } else {
    get(dnn::tau_dxy_pca_x) = 0;
    get(dnn::tau_dxy_pca_y) = 0;
    get(dnn::tau_dxy_pca_z) = 0;
  }
  const bool tau_dxy_valid =
      isAbove(tau_funcs.getdxy(tau, tau_index), -10) && isAbove(tau_funcs.getdxyError(tau, tau_index), 0);
  if (tau_dxy_valid) {
    get(dnn::tau_dxy_valid) = tau_dxy_valid;
    get(dnn::tau_dxy) = getValueNorm(tau_funcs.getdxy(tau, tau_index), 0.0018f, 0.0085f);
    get(dnn::tau_dxy_sig) =
        getValueNorm(std::abs(tau_funcs.getdxy(tau, tau_index)) / tau_funcs.getdxyError(tau, tau_index), 2.26f, 4.191f);
  }
  const bool tau_ip3d_valid =
      isAbove(tau_funcs.getip3d(tau, tau_index), -10) && isAbove(tau_funcs.getip3dError(tau, tau_index), 0);
  if (tau_ip3d_valid) {
    get(dnn::tau_ip3d_valid) = tau_ip3d_valid;
    get(dnn::tau_ip3d) = getValueNorm(tau_funcs.getip3d(tau, tau_index), 0.0026f, 0.0114f);
    get(dnn::tau_ip3d_sig) = getValueNorm(
        std::abs(tau_funcs.getip3d(tau, tau_index)) / tau_funcs.getip3dError(tau, tau_index), 2.928f, 4.466f);
  }
  if (leadChargedHadrCand) {
    const bool hasTrackDetails = candFunc::getHasTrackDetails(*leadChargedHadrCand);
    const float tau_dz = (!hasTrackDetails) ? 0 : candFunc::getTauDz(*leadChargedHadrCand);
    get(dnn::tau_dz) = getValueNorm(tau_dz, 0.f, 0.0190f);
    get(dnn::tau_dz_sig_valid) = candFunc::getTauDZSigValid(*leadChargedHadrCand);
    const double dzError = hasTrackDetails ? leadChargedHadrCand->dzError() : -999.;
    get(dnn::tau_dz_sig) = getValueNorm(std::abs(tau_dz) / dzError, 4.717f, 11.78f);
  }
  get(dnn::tau_flightLength_x) = getValueNorm(tau_funcs.getFlightLength(tau, tau_index).x(), -0.0003f, 0.7362f);
  get(dnn::tau_flightLength_y) = getValueNorm(tau_funcs.getFlightLength(tau, tau_index).y(), -0.0009f, 0.7354f);
  get(dnn::tau_flightLength_z) = getValueNorm(tau_funcs.getFlightLength(tau, tau_index).z(), -0.0022f, 1.993f);
  get(dnn::tau_flightLength_sig) = 0.55756444;  //This value is set due to a bug in the training
  get(dnn::tau_pt_weighted_deta_strip) =
      getValueLinear(reco::tau::pt_weighted_deta_strip(tau, tau.decayMode()), 0, 1, true);

  get(dnn::tau_pt_weighted_dphi_strip) =
      getValueLinear(reco::tau::pt_weighted_dphi_strip(tau, tau.decayMode()), 0, 1, true);
  get(dnn::tau_pt_weighted_dr_signal) =
      getValueNorm(reco::tau::pt_weighted_dr_signal(tau, tau.decayMode()), 0.0052f, 0.01433f);
  get(dnn::tau_pt_weighted_dr_iso) = getValueLinear(reco::tau::pt_weighted_dr_iso(tau, tau.decayMode()), 0, 1, true);
  get(dnn::tau_leadingTrackNormChi2) = getValueNorm(tau_funcs.getLeadingTrackNormChi2(tau), 1.538f, 4.401f);
  const auto eratio = reco::tau::eratio(tau);
  const bool tau_e_ratio_valid = std::isnormal(eratio) && eratio > 0.f;
  get(dnn::tau_e_ratio_valid) = tau_e_ratio_valid;
  get(dnn::tau_e_ratio) = tau_e_ratio_valid ? getValueLinear(eratio, 0, 1, true) : 0.f;
  const double gj_angle_diff = calculateGottfriedJacksonAngleDifference(tau, tau_index, tau_funcs);
  const bool tau_gj_angle_diff_valid = (std::isnormal(gj_angle_diff) || gj_angle_diff == 0) && gj_angle_diff >= 0;
  get(dnn::tau_gj_angle_diff_valid) = tau_gj_angle_diff_valid;
  get(dnn::tau_gj_angle_diff) = tau_gj_angle_diff_valid ? getValueLinear(gj_angle_diff, 0, pi, true) : 0;
  get(dnn::tau_n_photons) = getValueNorm(reco::tau::n_photons_total(tau), 2.95f, 3.927f);
  get(dnn::tau_emFraction) = getValueLinear(tau_funcs.getEmFraction(tau), -1, 1, false);

  get(dnn::tau_inside_ecal_crack) = getValue(isInEcalCrack(tau.p4().eta()));
  get(dnn::leadChargedCand_etaAtEcalEntrance_minus_tau_eta) =
      getValueNorm(tau_funcs.getEtaAtEcalEntrance(tau) - tau.p4().eta(), 0.0042f, 0.0323f);
}

template <typename CandidateCastType, typename TauCastType>
void DeepTauIdSonicProducer::createEgammaBlockInputs(unsigned idx,
                                                     const TauCastType& tau,
                                                     const size_t tau_index,
                                                     const edm::RefToBase<reco::BaseTau> tau_ref,
                                                     const reco::Vertex& pv,
                                                     double rho,
                                                     const std::vector<pat::Electron>* electrons,
                                                     const edm::View<reco::Candidate>& pfCands,
                                                     const Cell& cell_map,
                                                     TauFunc tau_funcs,
                                                     bool is_inner,
                                                     std::vector<float>& egammaBlockInputs) {
  namespace dnn = dnn_inputs_2017_v2::EgammaBlockInputs;

  const auto& get = [&](int var_index) -> float& {
    return egammaBlockInputs.at(var_index + idx * dnn_inputs_2017_v2::EgammaBlockInputs::NumberOfInputs);
  };

  const bool valid_index_pf_ele = cell_map.count(CellObjectType::PfCand_electron);
  const bool valid_index_pf_gamma = cell_map.count(CellObjectType::PfCand_gamma);
  const bool valid_index_ele = cell_map.count(CellObjectType::Electron);

  if (!cell_map.empty()) {
    get(dnn::rho) = getValueNorm(rho, 21.49f, 9.713f);
    get(dnn::tau_pt) = getValueLinear(tau.polarP4().pt(), 20.f, 1000.f, true);
    get(dnn::tau_eta) = getValueLinear(tau.polarP4().eta(), -2.3f, 2.3f, false);
    get(dnn::tau_inside_ecal_crack) = getValue(isInEcalCrack(tau.polarP4().eta()));
  }
  if (valid_index_pf_ele) {
    size_t index_pf_ele = cell_map.at(CellObjectType::PfCand_electron);
    const auto& ele_cand = dynamic_cast<const CandidateCastType&>(pfCands.at(index_pf_ele));

    get(dnn::pfCand_ele_valid) = valid_index_pf_ele;
    get(dnn::pfCand_ele_rel_pt) = getValueNorm(pfCands.at(index_pf_ele).polarP4().pt() / tau.polarP4().pt(),
                                               is_inner ? 0.9792f : 0.304f,
                                               is_inner ? 0.5383f : 1.845f);
    get(dnn::pfCand_ele_deta) = getValueLinear(pfCands.at(index_pf_ele).polarP4().eta() - tau.polarP4().eta(),
                                               is_inner ? -0.1f : -0.5f,
                                               is_inner ? 0.1f : 0.5f,
                                               false);
    get(dnn::pfCand_ele_dphi) = getValueLinear(dPhi(tau.polarP4(), pfCands.at(index_pf_ele).polarP4()),
                                               is_inner ? -0.1f : -0.5f,
                                               is_inner ? 0.1f : 0.5f,
                                               false);
    get(dnn::pfCand_ele_pvAssociationQuality) =
        getValueLinear<int>(candFunc::getPvAssocationQuality(ele_cand), 0, 7, true);
    get(dnn::pfCand_ele_puppiWeight) = is_inner ? getValue(candFunc::getPuppiWeight(ele_cand, 0.9906834f))
                                                : getValue(candFunc::getPuppiWeight(ele_cand, 0.9669586f));
    get(dnn::pfCand_ele_charge) = getValue(ele_cand.charge());
    get(dnn::pfCand_ele_lostInnerHits) = getValue<int>(candFunc::getLostInnerHits(ele_cand, 0));
    get(dnn::pfCand_ele_numberOfPixelHits) = getValueLinear(candFunc::getNumberOfPixelHits(ele_cand, 0), 0, 10, true);
    get(dnn::pfCand_ele_vertex_dx) =
        getValueNorm(pfCands.at(index_pf_ele).vertex().x() - pv.position().x(), 0.f, 0.1221f);
    get(dnn::pfCand_ele_vertex_dy) =
        getValueNorm(pfCands.at(index_pf_ele).vertex().y() - pv.position().y(), 0.f, 0.1226f);
    get(dnn::pfCand_ele_vertex_dz) =
        getValueNorm(pfCands.at(index_pf_ele).vertex().z() - pv.position().z(), 0.001f, 1.024f);
    get(dnn::pfCand_ele_vertex_dx_tauFL) = getValueNorm(
        pfCands.at(index_pf_ele).vertex().x() - pv.position().x() - tau_funcs.getFlightLength(tau, tau_index).x(),
        0.f,
        0.3411f);
    get(dnn::pfCand_ele_vertex_dy_tauFL) = getValueNorm(
        pfCands.at(index_pf_ele).vertex().y() - pv.position().y() - tau_funcs.getFlightLength(tau, tau_index).y(),
        0.0003f,
        0.3385f);
    get(dnn::pfCand_ele_vertex_dz_tauFL) = getValueNorm(
        pfCands.at(index_pf_ele).vertex().z() - pv.position().z() - tau_funcs.getFlightLength(tau, tau_index).z(),
        0.f,
        1.307f);

    const bool hasTrackDetails = candFunc::getHasTrackDetails(ele_cand);
    if (hasTrackDetails) {
      get(dnn::pfCand_ele_hasTrackDetails) = hasTrackDetails;
      get(dnn::pfCand_ele_dxy) = getValueNorm(candFunc::getTauDxy(ele_cand), 0.f, 0.171f);
      get(dnn::pfCand_ele_dxy_sig) =
          getValueNorm(std::abs(candFunc::getTauDxy(ele_cand)) / pfCands.at(index_pf_ele).dxyError(), 1.634f, 6.45f);
      get(dnn::pfCand_ele_dz) = getValueNorm(candFunc::getTauDz(ele_cand), 0.001f, 1.02f);
      get(dnn::pfCand_ele_dz_sig) =
          getValueNorm(std::abs(candFunc::getTauDz(ele_cand)) / ele_cand.dzError(), 24.56f, 210.4f);
      get(dnn::pfCand_ele_track_chi2_ndof) = getValueNorm(
          candFunc::getPseudoTrack(ele_cand).chi2() / candFunc::getPseudoTrack(ele_cand).ndof(), 2.272f, 8.439f);
      get(dnn::pfCand_ele_track_ndof) = getValueNorm(candFunc::getPseudoTrack(ele_cand).ndof(), 15.18f, 3.203f);
    }
  }
  if (valid_index_pf_gamma) {
    size_t index_pf_gamma = cell_map.at(CellObjectType::PfCand_gamma);
    const auto& gamma_cand = dynamic_cast<const CandidateCastType&>(pfCands.at(index_pf_gamma));

    get(dnn::pfCand_gamma_valid) = valid_index_pf_gamma;
    get(dnn::pfCand_gamma_rel_pt) = getValueNorm(pfCands.at(index_pf_gamma).polarP4().pt() / tau.polarP4().pt(),
                                                 is_inner ? 0.6048f : 0.02576f,
                                                 is_inner ? 1.669f : 0.3833f);
    get(dnn::pfCand_gamma_deta) = getValueLinear(pfCands.at(index_pf_gamma).polarP4().eta() - tau.polarP4().eta(),
                                                 is_inner ? -0.1f : -0.5f,
                                                 is_inner ? 0.1f : 0.5f,
                                                 false);
    get(dnn::pfCand_gamma_dphi) = getValueLinear(dPhi(tau.polarP4(), pfCands.at(index_pf_gamma).polarP4()),
                                                 is_inner ? -0.1f : -0.5f,
                                                 is_inner ? 0.1f : 0.5f,
                                                 false);
    get(dnn::pfCand_gamma_pvAssociationQuality) =
        getValueLinear<int>(candFunc::getPvAssocationQuality(gamma_cand), 0, 7, true);
    get(dnn::pfCand_gamma_fromPV) = getValueLinear<int>(candFunc::getFromPV(gamma_cand), 0, 3, true);
    get(dnn::pfCand_gamma_puppiWeight) = is_inner ? getValue(candFunc::getPuppiWeight(gamma_cand, 0.9084110f))
                                                  : getValue(candFunc::getPuppiWeight(gamma_cand, 0.4211567f));
    get(dnn::pfCand_gamma_puppiWeightNoLep) = is_inner
                                                  ? getValue(candFunc::getPuppiWeightNoLep(gamma_cand, 0.8857716f))
                                                  : getValue(candFunc::getPuppiWeightNoLep(gamma_cand, 0.3822604f));
    get(dnn::pfCand_gamma_lostInnerHits) = getValue<int>(candFunc::getLostInnerHits(gamma_cand, 0));
    get(dnn::pfCand_gamma_numberOfPixelHits) =
        getValueLinear(candFunc::getNumberOfPixelHits(gamma_cand, 0), 0, 7, true);
    get(dnn::pfCand_gamma_vertex_dx) =
        getValueNorm(pfCands.at(index_pf_gamma).vertex().x() - pv.position().x(), 0.f, 0.0067f);
    get(dnn::pfCand_gamma_vertex_dy) =
        getValueNorm(pfCands.at(index_pf_gamma).vertex().y() - pv.position().y(), 0.f, 0.0069f);
    get(dnn::pfCand_gamma_vertex_dz) =
        getValueNorm(pfCands.at(index_pf_gamma).vertex().z() - pv.position().z(), 0.f, 0.0578f);
    get(dnn::pfCand_gamma_vertex_dx_tauFL) = getValueNorm(
        pfCands.at(index_pf_gamma).vertex().x() - pv.position().x() - tau_funcs.getFlightLength(tau, tau_index).x(),
        0.001f,
        0.9565f);
    get(dnn::pfCand_gamma_vertex_dy_tauFL) = getValueNorm(
        pfCands.at(index_pf_gamma).vertex().y() - pv.position().y() - tau_funcs.getFlightLength(tau, tau_index).y(),
        0.0008f,
        0.9592f);
    get(dnn::pfCand_gamma_vertex_dz_tauFL) = getValueNorm(
        pfCands.at(index_pf_gamma).vertex().z() - pv.position().z() - tau_funcs.getFlightLength(tau, tau_index).z(),
        0.0038f,
        2.154f);
    const bool hasTrackDetails = candFunc::getHasTrackDetails(gamma_cand);
    if (hasTrackDetails) {
      get(dnn::pfCand_gamma_hasTrackDetails) = hasTrackDetails;
      get(dnn::pfCand_gamma_dxy) = getValueNorm(candFunc::getTauDxy(gamma_cand), 0.0004f, 0.882f);
      get(dnn::pfCand_gamma_dxy_sig) =
          getValueNorm(std::abs(candFunc::getTauDxy(gamma_cand)) / gamma_cand.dxyError(), 4.271f, 63.78f);
      get(dnn::pfCand_gamma_dz) = getValueNorm(candFunc::getTauDz(gamma_cand), 0.0071f, 5.285f);
      get(dnn::pfCand_gamma_dz_sig) =
          getValueNorm(std::abs(candFunc::getTauDz(gamma_cand)) / gamma_cand.dzError(), 162.1f, 622.4f);
      get(dnn::pfCand_gamma_track_chi2_ndof) =
          candFunc::getPseudoTrack(gamma_cand).ndof() > 0
              ? getValueNorm(candFunc::getPseudoTrack(gamma_cand).chi2() / candFunc::getPseudoTrack(gamma_cand).ndof(),
                             4.268f,
                             15.47f)
              : 0;
      get(dnn::pfCand_gamma_track_ndof) =
          candFunc::getPseudoTrack(gamma_cand).ndof() > 0
              ? getValueNorm(candFunc::getPseudoTrack(gamma_cand).ndof(), 12.25f, 4.774f)
              : 0;
    }
  }
  if (valid_index_ele) {
    size_t index_ele = cell_map.at(CellObjectType::Electron);

    get(dnn::ele_valid) = valid_index_ele;
    get(dnn::ele_rel_pt) = getValueNorm(electrons->at(index_ele).polarP4().pt() / tau.polarP4().pt(),
                                        is_inner ? 1.067f : 0.5111f,
                                        is_inner ? 1.521f : 2.765f);
    get(dnn::ele_deta) = getValueLinear(electrons->at(index_ele).polarP4().eta() - tau.polarP4().eta(),
                                        is_inner ? -0.1f : -0.5f,
                                        is_inner ? 0.1f : 0.5f,
                                        false);
    get(dnn::ele_dphi) = getValueLinear(dPhi(tau.polarP4(), electrons->at(index_ele).polarP4()),
                                        is_inner ? -0.1f : -0.5f,
                                        is_inner ? 0.1f : 0.5f,
                                        false);

    float cc_ele_energy, cc_gamma_energy;
    int cc_n_gamma;
    const bool cc_valid =
        calculateElectronClusterVarsV2(electrons->at(index_ele), cc_ele_energy, cc_gamma_energy, cc_n_gamma);
    if (cc_valid) {
      get(dnn::ele_cc_valid) = cc_valid;
      get(dnn::ele_cc_ele_rel_energy) =
          getValueNorm(cc_ele_energy / electrons->at(index_ele).polarP4().pt(), 1.729f, 1.644f);
      get(dnn::ele_cc_gamma_rel_energy) = getValueNorm(cc_gamma_energy / cc_ele_energy, 0.1439f, 0.3284f);
      get(dnn::ele_cc_n_gamma) = getValueNorm(cc_n_gamma, 1.794f, 2.079f);
    }
    get(dnn::ele_rel_trackMomentumAtVtx) = getValueNorm(
        electrons->at(index_ele).trackMomentumAtVtx().R() / electrons->at(index_ele).polarP4().pt(), 1.531f, 1.424f);
    get(dnn::ele_rel_trackMomentumAtCalo) = getValueNorm(
        electrons->at(index_ele).trackMomentumAtCalo().R() / electrons->at(index_ele).polarP4().pt(), 1.531f, 1.424f);
    get(dnn::ele_rel_trackMomentumOut) = getValueNorm(
        electrons->at(index_ele).trackMomentumOut().R() / electrons->at(index_ele).polarP4().pt(), 0.7735f, 0.935f);
    get(dnn::ele_rel_trackMomentumAtEleClus) =
        getValueNorm(electrons->at(index_ele).trackMomentumAtEleClus().R() / electrons->at(index_ele).polarP4().pt(),
                     0.7735f,
                     0.935f);
    get(dnn::ele_rel_trackMomentumAtVtxWithConstraint) = getValueNorm(
        electrons->at(index_ele).trackMomentumAtVtxWithConstraint().R() / electrons->at(index_ele).polarP4().pt(),
        1.625f,
        1.581f);
    get(dnn::ele_rel_ecalEnergy) =
        getValueNorm(electrons->at(index_ele).ecalEnergy() / electrons->at(index_ele).polarP4().pt(), 1.993f, 1.308f);
    get(dnn::ele_ecalEnergy_sig) = getValueNorm(
        electrons->at(index_ele).ecalEnergy() / electrons->at(index_ele).ecalEnergyError(), 70.25f, 58.16f);
    get(dnn::ele_eSuperClusterOverP) = getValueNorm(electrons->at(index_ele).eSuperClusterOverP(), 2.432f, 15.13f);
    get(dnn::ele_eSeedClusterOverP) = getValueNorm(electrons->at(index_ele).eSeedClusterOverP(), 2.034f, 13.96f);
    get(dnn::ele_eSeedClusterOverPout) = getValueNorm(electrons->at(index_ele).eSeedClusterOverPout(), 6.64f, 36.8f);
    get(dnn::ele_eEleClusterOverPout) = getValueNorm(electrons->at(index_ele).eEleClusterOverPout(), 4.183f, 20.63f);
    get(dnn::ele_deltaEtaSuperClusterTrackAtVtx) =
        getValueNorm(electrons->at(index_ele).deltaEtaSuperClusterTrackAtVtx(), 0.f, 0.0363f);
    get(dnn::ele_deltaEtaSeedClusterTrackAtCalo) =
        getValueNorm(electrons->at(index_ele).deltaEtaSeedClusterTrackAtCalo(), -0.0001f, 0.0512f);
    get(dnn::ele_deltaEtaEleClusterTrackAtCalo) =
        getValueNorm(electrons->at(index_ele).deltaEtaEleClusterTrackAtCalo(), -0.0001f, 0.0541f);
    get(dnn::ele_deltaPhiEleClusterTrackAtCalo) =
        getValueNorm(electrons->at(index_ele).deltaPhiEleClusterTrackAtCalo(), 0.0002f, 0.0553f);
    get(dnn::ele_deltaPhiSuperClusterTrackAtVtx) =
        getValueNorm(electrons->at(index_ele).deltaPhiSuperClusterTrackAtVtx(), 0.0001f, 0.0523f);
    get(dnn::ele_deltaPhiSeedClusterTrackAtCalo) =
        getValueNorm(electrons->at(index_ele).deltaPhiSeedClusterTrackAtCalo(), 0.0004f, 0.0777f);
    get(dnn::ele_mvaInput_earlyBrem) = getValue(electrons->at(index_ele).mvaInput().earlyBrem);
    get(dnn::ele_mvaInput_lateBrem) = getValue(electrons->at(index_ele).mvaInput().lateBrem);
    get(dnn::ele_mvaInput_sigmaEtaEta) =
        getValueNorm(electrons->at(index_ele).mvaInput().sigmaEtaEta, 0.0008f, 0.0052f);
    get(dnn::ele_mvaInput_hadEnergy) = getValueNorm(electrons->at(index_ele).mvaInput().hadEnergy, 14.04f, 69.48f);
    get(dnn::ele_mvaInput_deltaEta) = getValueNorm(electrons->at(index_ele).mvaInput().deltaEta, 0.0099f, 0.0851f);
    const auto& gsfTrack = electrons->at(index_ele).gsfTrack();
    if (gsfTrack.isNonnull()) {
      get(dnn::ele_gsfTrack_normalizedChi2) = getValueNorm(gsfTrack->normalizedChi2(), 3.049f, 10.39f);
      get(dnn::ele_gsfTrack_numberOfValidHits) = getValueNorm(gsfTrack->numberOfValidHits(), 16.52f, 2.806f);
      get(dnn::ele_rel_gsfTrack_pt) =
          getValueNorm(gsfTrack->pt() / electrons->at(index_ele).polarP4().pt(), 1.355f, 16.81f);
      get(dnn::ele_gsfTrack_pt_sig) = getValueNorm(gsfTrack->pt() / gsfTrack->ptError(), 5.046f, 3.119f);
    }
    const auto& closestCtfTrack = electrons->at(index_ele).closestCtfTrackRef();
    const bool has_closestCtfTrack = closestCtfTrack.isNonnull();
    if (has_closestCtfTrack) {
      get(dnn::ele_has_closestCtfTrack) = has_closestCtfTrack;
      get(dnn::ele_closestCtfTrack_normalizedChi2) = getValueNorm(closestCtfTrack->normalizedChi2(), 2.411f, 6.98f);
      get(dnn::ele_closestCtfTrack_numberOfValidHits) =
          getValueNorm(closestCtfTrack->numberOfValidHits(), 15.16f, 5.26f);
    }
  }
}

template <typename CandidateCastType, typename TauCastType>
void DeepTauIdSonicProducer::createMuonBlockInputs(unsigned idx,
                                                   const TauCastType& tau,
                                                   const size_t tau_index,
                                                   const edm::RefToBase<reco::BaseTau> tau_ref,
                                                   const reco::Vertex& pv,
                                                   double rho,
                                                   const std::vector<pat::Muon>* muons,
                                                   const edm::View<reco::Candidate>& pfCands,
                                                   const Cell& cell_map,
                                                   TauFunc tau_funcs,
                                                   bool is_inner,
                                                   std::vector<float>& muonBlockInputs) {
  namespace dnn = dnn_inputs_2017_v2::MuonBlockInputs;

  const auto& get = [&](int var_index) -> float& {
    return muonBlockInputs.at(var_index + idx * dnn_inputs_2017_v2::MuonBlockInputs::NumberOfInputs);
  };

  const bool valid_index_pf_muon = cell_map.count(CellObjectType::PfCand_muon);
  const bool valid_index_muon = cell_map.count(CellObjectType::Muon);

  if (!cell_map.empty()) {
    get(dnn::rho) = getValueNorm(rho, 21.49f, 9.713f);
    get(dnn::tau_pt) = getValueLinear(tau.polarP4().pt(), 20.f, 1000.f, true);
    get(dnn::tau_eta) = getValueLinear(tau.polarP4().eta(), -2.3f, 2.3f, false);
    get(dnn::tau_inside_ecal_crack) = getValue(isInEcalCrack(tau.polarP4().eta()));
  }
  if (valid_index_pf_muon) {
    size_t index_pf_muon = cell_map.at(CellObjectType::PfCand_muon);
    const auto& muon_cand = dynamic_cast<const CandidateCastType&>(pfCands.at(index_pf_muon));

    get(dnn::pfCand_muon_valid) = valid_index_pf_muon;
    get(dnn::pfCand_muon_rel_pt) = getValueNorm(pfCands.at(index_pf_muon).polarP4().pt() / tau.polarP4().pt(),
                                                is_inner ? 0.9509f : 0.0861f,
                                                is_inner ? 0.4294f : 0.4065f);
    get(dnn::pfCand_muon_deta) = getValueLinear(pfCands.at(index_pf_muon).polarP4().eta() - tau.polarP4().eta(),
                                                is_inner ? -0.1f : -0.5f,
                                                is_inner ? 0.1f : 0.5f,
                                                false);
    get(dnn::pfCand_muon_dphi) = getValueLinear(dPhi(tau.polarP4(), pfCands.at(index_pf_muon).polarP4()),
                                                is_inner ? -0.1f : -0.5f,
                                                is_inner ? 0.1f : 0.5f,
                                                false);
    get(dnn::pfCand_muon_pvAssociationQuality) =
        getValueLinear<int>(candFunc::getPvAssocationQuality(muon_cand), 0, 7, true);
    get(dnn::pfCand_muon_fromPV) = getValueLinear<int>(candFunc::getFromPV(muon_cand), 0, 3, true);
    get(dnn::pfCand_muon_puppiWeight) = is_inner ? getValue(candFunc::getPuppiWeight(muon_cand, 0.9786588f))
                                                 : getValue(candFunc::getPuppiWeight(muon_cand, 0.8132477f));
    get(dnn::pfCand_muon_charge) = getValue(muon_cand.charge());
    get(dnn::pfCand_muon_lostInnerHits) = getValue<int>(candFunc::getLostInnerHits(muon_cand, 0));
    get(dnn::pfCand_muon_numberOfPixelHits) = getValueLinear(candFunc::getNumberOfPixelHits(muon_cand, 0), 0, 11, true);
    get(dnn::pfCand_muon_vertex_dx) =
        getValueNorm(pfCands.at(index_pf_muon).vertex().x() - pv.position().x(), -0.0007f, 0.6869f);
    get(dnn::pfCand_muon_vertex_dy) =
        getValueNorm(pfCands.at(index_pf_muon).vertex().y() - pv.position().y(), 0.0001f, 0.6784f);
    get(dnn::pfCand_muon_vertex_dz) =
        getValueNorm(pfCands.at(index_pf_muon).vertex().z() - pv.position().z(), -0.0117f, 4.097f);
    get(dnn::pfCand_muon_vertex_dx_tauFL) = getValueNorm(
        pfCands.at(index_pf_muon).vertex().x() - pv.position().x() - tau_funcs.getFlightLength(tau, tau_index).x(),
        -0.0001f,
        0.8642f);
    get(dnn::pfCand_muon_vertex_dy_tauFL) = getValueNorm(
        pfCands.at(index_pf_muon).vertex().y() - pv.position().y() - tau_funcs.getFlightLength(tau, tau_index).y(),
        0.0004f,
        0.8561f);
    get(dnn::pfCand_muon_vertex_dz_tauFL) = getValueNorm(
        pfCands.at(index_pf_muon).vertex().z() - pv.position().z() - tau_funcs.getFlightLength(tau, tau_index).z(),
        -0.0118f,
        4.405f);

    const bool hasTrackDetails = candFunc::getHasTrackDetails(muon_cand);
    if (hasTrackDetails) {
      get(dnn::pfCand_muon_hasTrackDetails) = hasTrackDetails;
      get(dnn::pfCand_muon_dxy) = getValueNorm(candFunc::getTauDxy(muon_cand), -0.0045f, 0.9655f);
      get(dnn::pfCand_muon_dxy_sig) =
          getValueNorm(std::abs(candFunc::getTauDxy(muon_cand)) / muon_cand.dxyError(), 4.575f, 42.36f);
      get(dnn::pfCand_muon_dz) = getValueNorm(candFunc::getTauDz(muon_cand), -0.0117f, 4.097f);
      get(dnn::pfCand_muon_dz_sig) =
          getValueNorm(std::abs(candFunc::getTauDz(muon_cand)) / muon_cand.dzError(), 80.37f, 343.3f);
      get(dnn::pfCand_muon_track_chi2_ndof) = getValueNorm(
          candFunc::getPseudoTrack(muon_cand).chi2() / candFunc::getPseudoTrack(muon_cand).ndof(), 0.69f, 1.711f);
      get(dnn::pfCand_muon_track_ndof) = getValueNorm(candFunc::getPseudoTrack(muon_cand).ndof(), 17.5f, 5.11f);
    }
  }
  if (valid_index_muon) {
    size_t index_muon = cell_map.at(CellObjectType::Muon);

    get(dnn::muon_valid) = valid_index_muon;
    get(dnn::muon_rel_pt) = getValueNorm(muons->at(index_muon).polarP4().pt() / tau.polarP4().pt(),
                                         is_inner ? 0.7966f : 0.2678f,
                                         is_inner ? 3.402f : 3.592f);
    get(dnn::muon_deta) = getValueLinear(muons->at(index_muon).polarP4().eta() - tau.polarP4().eta(),
                                         is_inner ? -0.1f : -0.5f,
                                         is_inner ? 0.1f : 0.5f,
                                         false);
    get(dnn::muon_dphi) = getValueLinear(
        dPhi(tau.polarP4(), muons->at(index_muon).polarP4()), is_inner ? -0.1f : -0.5f, is_inner ? 0.1f : 0.5f, false);
    get(dnn::muon_dxy) = getValueNorm(muons->at(index_muon).dB(pat::Muon::PV2D), 0.0019f, 1.039f);
    get(dnn::muon_dxy_sig) =
        getValueNorm(std::abs(muons->at(index_muon).dB(pat::Muon::PV2D)) / muons->at(index_muon).edB(pat::Muon::PV2D),
                     8.98f,
                     71.17f);

    const bool normalizedChi2_valid =
        muons->at(index_muon).globalTrack().isNonnull() && muons->at(index_muon).normChi2() >= 0;
    if (normalizedChi2_valid) {
      get(dnn::muon_normalizedChi2_valid) = normalizedChi2_valid;
      get(dnn::muon_normalizedChi2) = getValueNorm(muons->at(index_muon).normChi2(), 21.52f, 265.8f);
      if (muons->at(index_muon).innerTrack().isNonnull())
        get(dnn::muon_numberOfValidHits) = getValueNorm(muons->at(index_muon).numberOfValidHits(), 21.84f, 10.59f);
    }
    get(dnn::muon_segmentCompatibility) = getValue(muons->at(index_muon).segmentCompatibility());
    get(dnn::muon_caloCompatibility) = getValue(muons->at(index_muon).caloCompatibility());

    const bool pfEcalEnergy_valid = muons->at(index_muon).pfEcalEnergy() >= 0;
    if (pfEcalEnergy_valid) {
      get(dnn::muon_pfEcalEnergy_valid) = pfEcalEnergy_valid;
      get(dnn::muon_rel_pfEcalEnergy) =
          getValueNorm(muons->at(index_muon).pfEcalEnergy() / muons->at(index_muon).polarP4().pt(), 0.2273f, 0.4865f);
    }

    MuonHitMatchV2 hit_match(muons->at(index_muon));
    static const std::map<int, std::pair<int, int>> muonMatchHitVars = {
        {MuonSubdetId::DT, {dnn::muon_n_matches_DT_1, dnn::muon_n_hits_DT_1}},
        {MuonSubdetId::CSC, {dnn::muon_n_matches_CSC_1, dnn::muon_n_hits_CSC_1}},
        {MuonSubdetId::RPC, {dnn::muon_n_matches_RPC_1, dnn::muon_n_hits_RPC_1}}};

    static const std::map<int, std::vector<float>> muonMatchVarLimits = {
        {MuonSubdetId::DT, {2, 2, 2, 2}}, {MuonSubdetId::CSC, {6, 2, 2, 2}}, {MuonSubdetId::RPC, {7, 6, 4, 4}}};

    static const std::map<int, std::vector<float>> muonHitVarLimits = {
        {MuonSubdetId::DT, {12, 12, 12, 8}}, {MuonSubdetId::CSC, {24, 12, 12, 12}}, {MuonSubdetId::RPC, {4, 4, 2, 2}}};

    for (int subdet : hit_match.MuonHitMatchV2::consideredSubdets()) {
      const auto& matchHitVar = muonMatchHitVars.at(subdet);
      const auto& matchLimits = muonMatchVarLimits.at(subdet);
      const auto& hitLimits = muonHitVarLimits.at(subdet);
      for (int station = MuonHitMatchV2::first_station_id; station <= MuonHitMatchV2::last_station_id; ++station) {
        const unsigned n_matches = hit_match.nMatches(subdet, station);
        const unsigned n_hits = hit_match.nHits(subdet, station);
        get(matchHitVar.first + station - 1) = getValueLinear(n_matches, 0, matchLimits.at(station - 1), true);
        get(matchHitVar.second + station - 1) = getValueLinear(n_hits, 0, hitLimits.at(station - 1), true);
      }
    }
  }
}

template <typename CandidateCastType, typename TauCastType>
void DeepTauIdSonicProducer::createHadronsBlockInputs(unsigned idx,
                                                      const TauCastType& tau,
                                                      const size_t tau_index,
                                                      const edm::RefToBase<reco::BaseTau> tau_ref,
                                                      const reco::Vertex& pv,
                                                      double rho,
                                                      const edm::View<reco::Candidate>& pfCands,
                                                      const Cell& cell_map,
                                                      TauFunc tau_funcs,
                                                      bool is_inner,
                                                      std::vector<float>& hadronBlockInputs) {
  namespace dnn = dnn_inputs_2017_v2::HadronBlockInputs;

  const auto& get = [&](int var_index) -> float& {
    return hadronBlockInputs.at(var_index + idx * dnn_inputs_2017_v2::HadronBlockInputs::NumberOfInputs);
  };

  const bool valid_chH = cell_map.count(CellObjectType::PfCand_chargedHadron);
  const bool valid_nH = cell_map.count(CellObjectType::PfCand_neutralHadron);

  if (!cell_map.empty()) {
    get(dnn::rho) = getValueNorm(rho, 21.49f, 9.713f);
    get(dnn::tau_pt) = getValueLinear(tau.polarP4().pt(), 20.f, 1000.f, true);
    get(dnn::tau_eta) = getValueLinear(tau.polarP4().eta(), -2.3f, 2.3f, false);
    get(dnn::tau_inside_ecal_crack) = getValue(isInEcalCrack(tau.polarP4().eta()));
  }
  if (valid_chH) {
    size_t index_chH = cell_map.at(CellObjectType::PfCand_chargedHadron);
    const auto& chH_cand = dynamic_cast<const CandidateCastType&>(pfCands.at(index_chH));

    get(dnn::pfCand_chHad_valid) = valid_chH;
    get(dnn::pfCand_chHad_rel_pt) = getValueNorm(pfCands.at(index_chH).polarP4().pt() / tau.polarP4().pt(),
                                                 is_inner ? 0.2564f : 0.0194f,
                                                 is_inner ? 0.8607f : 0.1865f);
    get(dnn::pfCand_chHad_deta) = getValueLinear(pfCands.at(index_chH).polarP4().eta() - tau.polarP4().eta(),
                                                 is_inner ? -0.1f : -0.5f,
                                                 is_inner ? 0.1f : 0.5f,
                                                 false);
    get(dnn::pfCand_chHad_dphi) = getValueLinear(
        dPhi(tau.polarP4(), pfCands.at(index_chH).polarP4()), is_inner ? -0.1f : -0.5f, is_inner ? 0.1f : 0.5f, false);
    get(dnn::pfCand_chHad_leadChargedHadrCand) =
        getValue(&chH_cand == dynamic_cast<const CandidateCastType*>(tau.leadChargedHadrCand().get()));
    get(dnn::pfCand_chHad_pvAssociationQuality) =
        getValueLinear<int>(candFunc::getPvAssocationQuality(chH_cand), 0, 7, true);
    get(dnn::pfCand_chHad_fromPV) = getValueLinear<int>(candFunc::getFromPV(chH_cand), 0, 3, true);
    const float default_chH_pw_inner = 0.7614090f;
    const float default_chH_pw_outer = 0.1974930f;
    get(dnn::pfCand_chHad_puppiWeight) = is_inner ? getValue(candFunc::getPuppiWeight(chH_cand, default_chH_pw_inner))
                                                  : getValue(candFunc::getPuppiWeight(chH_cand, default_chH_pw_outer));
    get(dnn::pfCand_chHad_puppiWeightNoLep) =
        is_inner ? getValue(candFunc::getPuppiWeightNoLep(chH_cand, default_chH_pw_inner))
                 : getValue(candFunc::getPuppiWeightNoLep(chH_cand, default_chH_pw_outer));
    get(dnn::pfCand_chHad_charge) = getValue(chH_cand.charge());
    get(dnn::pfCand_chHad_lostInnerHits) = getValue<int>(candFunc::getLostInnerHits(chH_cand, 0));
    get(dnn::pfCand_chHad_numberOfPixelHits) = getValueLinear(candFunc::getNumberOfPixelHits(chH_cand, 0), 0, 12, true);
    get(dnn::pfCand_chHad_vertex_dx) =
        getValueNorm(pfCands.at(index_chH).vertex().x() - pv.position().x(), 0.0005f, 1.735f);
    get(dnn::pfCand_chHad_vertex_dy) =
        getValueNorm(pfCands.at(index_chH).vertex().y() - pv.position().y(), -0.0008f, 1.752f);
    get(dnn::pfCand_chHad_vertex_dz) =
        getValueNorm(pfCands.at(index_chH).vertex().z() - pv.position().z(), -0.0201f, 8.333f);
    get(dnn::pfCand_chHad_vertex_dx_tauFL) = getValueNorm(
        pfCands.at(index_chH).vertex().x() - pv.position().x() - tau_funcs.getFlightLength(tau, tau_index).x(),
        -0.0014f,
        1.93f);
    get(dnn::pfCand_chHad_vertex_dy_tauFL) = getValueNorm(
        pfCands.at(index_chH).vertex().y() - pv.position().y() - tau_funcs.getFlightLength(tau, tau_index).y(),
        0.0022f,
        1.948f);
    get(dnn::pfCand_chHad_vertex_dz_tauFL) = getValueNorm(
        pfCands.at(index_chH).vertex().z() - pv.position().z() - tau_funcs.getFlightLength(tau, tau_index).z(),
        -0.0138f,
        8.622f);

    const bool hasTrackDetails = candFunc::getHasTrackDetails(chH_cand);
    if (hasTrackDetails) {
      get(dnn::pfCand_chHad_hasTrackDetails) = hasTrackDetails;
      get(dnn::pfCand_chHad_dxy) = getValueNorm(candFunc::getTauDxy(chH_cand), -0.012f, 2.386f);
      get(dnn::pfCand_chHad_dxy_sig) =
          getValueNorm(std::abs(candFunc::getTauDxy(chH_cand)) / chH_cand.dxyError(), 6.417f, 36.28f);
      get(dnn::pfCand_chHad_dz) = getValueNorm(candFunc::getTauDz(chH_cand), -0.0246f, 7.618f);
      get(dnn::pfCand_chHad_dz_sig) =
          getValueNorm(std::abs(candFunc::getTauDz(chH_cand)) / chH_cand.dzError(), 301.3f, 491.1f);
      get(dnn::pfCand_chHad_track_chi2_ndof) =
          candFunc::getPseudoTrack(chH_cand).ndof() > 0
              ? getValueNorm(candFunc::getPseudoTrack(chH_cand).chi2() / candFunc::getPseudoTrack(chH_cand).ndof(),
                             0.7876f,
                             3.694f)
              : 0;
      get(dnn::pfCand_chHad_track_ndof) = candFunc::getPseudoTrack(chH_cand).ndof() > 0
                                              ? getValueNorm(candFunc::getPseudoTrack(chH_cand).ndof(), 13.92f, 6.581f)
                                              : 0;
    }
    float hcal_fraction = candFunc::getHCalFraction(chH_cand, disable_hcalFraction_workaround_);
    get(dnn::pfCand_chHad_hcalFraction) = getValue(hcal_fraction);
    get(dnn::pfCand_chHad_rawCaloFraction) = getValueLinear(candFunc::getRawCaloFraction(chH_cand), 0.f, 2.6f, true);
  }
  if (valid_nH) {
    size_t index_nH = cell_map.at(CellObjectType::PfCand_neutralHadron);
    const auto& nH_cand = dynamic_cast<const CandidateCastType&>(pfCands.at(index_nH));

    get(dnn::pfCand_nHad_valid) = valid_nH;
    get(dnn::pfCand_nHad_rel_pt) = getValueNorm(pfCands.at(index_nH).polarP4().pt() / tau.polarP4().pt(),
                                                is_inner ? 0.3163f : 0.0502f,
                                                is_inner ? 0.2769f : 0.4266f);
    get(dnn::pfCand_nHad_deta) = getValueLinear(pfCands.at(index_nH).polarP4().eta() - tau.polarP4().eta(),
                                                is_inner ? -0.1f : -0.5f,
                                                is_inner ? 0.1f : 0.5f,
                                                false);
    get(dnn::pfCand_nHad_dphi) = getValueLinear(
        dPhi(tau.polarP4(), pfCands.at(index_nH).polarP4()), is_inner ? -0.1f : -0.5f, is_inner ? 0.1f : 0.5f, false);
    get(dnn::pfCand_nHad_puppiWeight) = is_inner ? getValue(candFunc::getPuppiWeight(nH_cand, 0.9798355f))
                                                 : getValue(candFunc::getPuppiWeight(nH_cand, 0.7813260f));
    get(dnn::pfCand_nHad_puppiWeightNoLep) = is_inner ? getValue(candFunc::getPuppiWeightNoLep(nH_cand, 0.9046796f))
                                                      : getValue(candFunc::getPuppiWeightNoLep(nH_cand, 0.6554860f));
    float hcal_fraction = candFunc::getHCalFraction(nH_cand, disable_hcalFraction_workaround_);
    get(dnn::pfCand_nHad_hcalFraction) = getValue(hcal_fraction);
  }
}

void DeepTauIdSonicProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  TritonClient::fillPSetDescription(desc);
  desc.add<edm::InputTag>("electrons", edm::InputTag("slimmedElectrons"));
  desc.add<edm::InputTag>("muons", edm::InputTag("slimmedMuons"));
  desc.add<edm::InputTag>("taus", edm::InputTag("slimmedTaus"));
  desc.add<edm::InputTag>("pfcands", edm::InputTag("packedPFCandidates"));
  desc.add<edm::InputTag>("vertices", edm::InputTag("offlineSlimmedPrimaryVertices"));
  desc.add<edm::InputTag>("rho", edm::InputTag("fixedGridRhoAll"));
  desc.add<bool>("mem_mapped", false);
  desc.add<unsigned>("version", 2);
  desc.add<int>("debug_level", 0);
  desc.add<bool>("disable_dxy_pca", false);
  desc.add<bool>("disable_hcalFraction_workaround", false);
  desc.add<bool>("disable_CellIndex_workaround", false);

  desc.add<std::vector<std::string>>("VSeWP");
  desc.add<std::vector<std::string>>("VSmuWP");
  desc.add<std::vector<std::string>>("VSjetWP");

  desc.addUntracked<edm::InputTag>("basicTauDiscriminators", edm::InputTag("basicTauDiscriminators"));
  desc.addUntracked<edm::InputTag>("basicTauDiscriminatorsdR03", edm::InputTag("basicTauDiscriminatorsdR03"));
  desc.add<edm::InputTag>("pfTauTransverseImpactParameters", edm::InputTag("hpsPFTauTransverseImpactParameters"));

  {
    edm::ParameterSetDescription pset_Prediscriminants;
    pset_Prediscriminants.add<std::string>("BooleanOperator", "and");
    {
      edm::ParameterSetDescription psd1;
      psd1.add<double>("cut");
      psd1.add<edm::InputTag>("Producer");
      pset_Prediscriminants.addOptional<edm::ParameterSetDescription>("decayMode", psd1);
    }
    desc.add<edm::ParameterSetDescription>("Prediscriminants", pset_Prediscriminants);
  }

  descriptions.add("DeepTauIdSonicProducer", desc);
}

DEFINE_FWK_MODULE(DeepTauIdSonicProducer);
