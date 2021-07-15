#ifndef RecoTauTag_RecoTau_DeepTauBase_h
#define RecoTauTag_RecoTau_DeepTauBase_h

/*
 * \class DeepTauBase
 *
 * Definition of the base class for tau identification using Deep NN.
 *
 * \author Konstantin Androsov, INFN Pisa
 * \author Maria Rosaria Di Domenico, University of Siena & INFN Pisa
 */

#include <Math/VectorUtil.h>
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "PhysicsTools/TensorFlow/interface/TensorFlow.h"
#include "tensorflow/core/util/memmapped_file_system.h"
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
#include "DataFormats/TauReco/interface/PFTauTransverseImpactParameterAssociation.h"
#include "FWCore/Utilities/interface/isFinite.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include <TF1.h>
#include <map>

#include <fstream>
#include "tbb/concurrent_unordered_set.h"

namespace deep_tau {

  class TauWPThreshold {
  public:
    explicit TauWPThreshold(const std::string& cut_str);
    double operator()(const reco::BaseTau& tau, bool isPFTau) const;

  private:
    std::unique_ptr<TF1> fn_;
    double value_;
  };

  class DeepTauCache {
  public:
    using GraphPtr = std::shared_ptr<tensorflow::GraphDef>;

    DeepTauCache(const std::map<std::string, std::string>& graph_names, bool mem_mapped);
    ~DeepTauCache();

    // A Session allows concurrent calls to Run(), though a Session must
    // be created / extended by a single thread.
    tensorflow::Session& getSession(const std::string& name = "") const { return *sessions_.at(name); }
    const tensorflow::GraphDef& getGraph(const std::string& name = "") const { return *graphs_.at(name); }

  private:
    std::map<std::string, GraphPtr> graphs_;
    std::map<std::string, tensorflow::Session*> sessions_;
    std::map<std::string, std::unique_ptr<tensorflow::MemmappedEnv>> memmappedEnv_;
  };

  class DeepTauBase : public edm::stream::EDProducer<edm::GlobalCache<DeepTauCache>> {
  public:
    using TauDiscriminator = reco::TauDiscriminatorContainer;
    using TauCollection = edm::View<reco::BaseTau>;
    using CandidateCollection = edm::View<reco::Candidate>;
    using TauRef = edm::Ref<TauCollection>;
    using TauRefProd = edm::RefProd<TauCollection>;
    using ElectronCollection = pat::ElectronCollection;
    using MuonCollection = pat::MuonCollection;
    using LorentzVectorXYZ = ROOT::Math::LorentzVector<ROOT::Math::PxPyPzE4D<double>>;
    using Cutter = TauWPThreshold;
    using CutterPtr = std::unique_ptr<Cutter>;
    using WPList = std::vector<CutterPtr>;

    struct Output {
      std::vector<size_t> num_, den_;

      Output(const std::vector<size_t>& num, const std::vector<size_t>& den) : num_(num), den_(den) {}

      float get_number(const tensorflow::Tensor& pred, size_t tau_index, size_t elem) const;
      float get_number(const std::vector<std::vector<float>>& pred, size_t tau_index, size_t elem) const;

      template <typename PredType>
      std::unique_ptr<TauDiscriminator> get_value(const edm::Handle<TauCollection>& taus,
                                                  const PredType& pred,
                                                  const WPList* working_points,
                                                  bool is_online) const {
        std::vector<reco::SingleTauDiscriminatorContainer> outputbuffer(taus->size());

        for (size_t tau_index = 0; tau_index < taus->size(); ++tau_index) {
          float x = 0;
          for (size_t num_elem : num_)
            x += get_number(pred, tau_index, num_elem);
          if (x != 0 && !den_.empty()) {
            float den_val = 0;
            for (size_t den_elem : den_)
              den_val += get_number(pred, tau_index, den_elem);
            x = den_val != 0 ? x / den_val : std::numeric_limits<float>::max();
          }
          outputbuffer[tau_index].rawValues.push_back(x);
          if (working_points) {
            for (const auto& wp : *working_points) {
              const bool pass = x > (*wp)(taus->at(tau_index), is_online);
              outputbuffer[tau_index].workingPoints.push_back(pass);
            }
          }
        }
        std::unique_ptr<TauDiscriminator> output = std::make_unique<TauDiscriminator>();
        reco::TauDiscriminatorContainer::Filler filler(*output);
        filler.insert(taus, outputbuffer.begin(), outputbuffer.end());
        filler.fill();
        return output;
      }
    };

    using OutputCollection = std::map<std::string, Output>;

    DeepTauBase(const edm::ParameterSet& cfg, const OutputCollection& outputs, const DeepTauCache* cache);
    ~DeepTauBase() override {}

    void produce(edm::Event& event, const edm::EventSetup& es) override;

    static std::unique_ptr<DeepTauCache> initializeGlobalCache(const edm::ParameterSet& cfg);
    static void globalEndJob(const DeepTauCache* cache) {}

    template <typename ConsumeType>
    struct TauDiscInfo {
      edm::InputTag label;
      edm::Handle<ConsumeType> handle;
      edm::EDGetTokenT<ConsumeType> disc_token;
      double cut;
      void fill(const edm::Event& evt) { evt.getByToken(disc_token, handle); }
    };

    // select boolean operation on prediscriminants (and = 0x01, or = 0x00)
    uint8_t andPrediscriminants_;
    std::vector<TauDiscInfo<pat::PATTauDiscriminator>> patPrediscriminants_;
    std::vector<TauDiscInfo<reco::PFTauDiscriminator>> recoPrediscriminants_;

    enum BasicDiscriminator {
      ChargedIsoPtSum,
      NeutralIsoPtSum,
      NeutralIsoPtSumWeight,
      FootprintCorrection,
      PhotonPtSumOutsideSignalCone,
      PUcorrPtSum
    };

  private:
    virtual tensorflow::Tensor getPredictions(edm::Event& event, edm::Handle<TauCollection> taus) = 0;
    virtual void createOutputs(edm::Event& event, const tensorflow::Tensor& pred, edm::Handle<TauCollection> taus);

  protected:
    edm::EDGetTokenT<TauCollection> tausToken_;
    edm::EDGetTokenT<CandidateCollection> pfcandToken_;
    edm::EDGetTokenT<reco::VertexCollection> vtxToken_;
    std::map<std::string, WPList> workingPoints_;
    const bool is_online_;
    OutputCollection outputs_;
    const DeepTauCache* cache_;

    static const std::map<BasicDiscriminator, std::string> stringFromDiscriminator_;
    static const std::vector<BasicDiscriminator> requiredBasicDiscriminators_;
    static const std::vector<BasicDiscriminator> requiredBasicDiscriminatorsdR03_;
  };

}  // namespace deep_tau

namespace deep_tau_2017 {
  struct dnn_inputs_2017v1 {
    enum vars {
      pt = 0,
      eta,
      mass,
      decayMode,
      chargedIsoPtSum,
      neutralIsoPtSum,
      neutralIsoPtSumWeight,
      photonPtSumOutsideSignalCone,
      puCorrPtSum,
      dxy,
      dxy_sig,
      dz,
      ip3d,
      ip3d_sig,
      hasSecondaryVertex,
      flightLength_r,
      flightLength_dEta,
      flightLength_dPhi,
      flightLength_sig,
      leadChargedHadrCand_pt,
      leadChargedHadrCand_dEta,
      leadChargedHadrCand_dPhi,
      leadChargedHadrCand_mass,
      pt_weighted_deta_strip,
      pt_weighted_dphi_strip,
      pt_weighted_dr_signal,
      pt_weighted_dr_iso,
      leadingTrackNormChi2,
      e_ratio,
      gj_angle_diff,
      n_photons,
      emFraction,
      has_gsf_track,
      inside_ecal_crack,
      gsf_ele_matched,
      gsf_ele_pt,
      gsf_ele_dEta,
      gsf_ele_dPhi,
      gsf_ele_mass,
      gsf_ele_Ee,
      gsf_ele_Egamma,
      gsf_ele_Pin,
      gsf_ele_Pout,
      gsf_ele_EtotOverPin,
      gsf_ele_Eecal,
      gsf_ele_dEta_SeedClusterTrackAtCalo,
      gsf_ele_dPhi_SeedClusterTrackAtCalo,
      gsf_ele_mvaIn_sigmaEtaEta,
      gsf_ele_mvaIn_hadEnergy,
      gsf_ele_mvaIn_deltaEta,
      gsf_ele_Chi2NormGSF,
      gsf_ele_GSFNumHits,
      gsf_ele_GSFTrackResol,
      gsf_ele_GSFTracklnPt,
      gsf_ele_Chi2NormKF,
      gsf_ele_KFNumHits,
      leadChargedCand_etaAtEcalEntrance,
      leadChargedCand_pt,
      leadChargedHadrCand_HoP,
      leadChargedHadrCand_EoP,
      tau_visMass_innerSigCone,
      n_matched_muons,
      muon_pt,
      muon_dEta,
      muon_dPhi,
      muon_n_matches_DT_1,
      muon_n_matches_DT_2,
      muon_n_matches_DT_3,
      muon_n_matches_DT_4,
      muon_n_matches_CSC_1,
      muon_n_matches_CSC_2,
      muon_n_matches_CSC_3,
      muon_n_matches_CSC_4,
      muon_n_hits_DT_2,
      muon_n_hits_DT_3,
      muon_n_hits_DT_4,
      muon_n_hits_CSC_2,
      muon_n_hits_CSC_3,
      muon_n_hits_CSC_4,
      muon_n_hits_RPC_2,
      muon_n_hits_RPC_3,
      muon_n_hits_RPC_4,
      muon_n_stations_with_matches_03,
      muon_n_stations_with_hits_23,
      signalChargedHadrCands_sum_innerSigCone_pt,
      signalChargedHadrCands_sum_innerSigCone_dEta,
      signalChargedHadrCands_sum_innerSigCone_dPhi,
      signalChargedHadrCands_sum_innerSigCone_mass,
      signalChargedHadrCands_sum_outerSigCone_pt,
      signalChargedHadrCands_sum_outerSigCone_dEta,
      signalChargedHadrCands_sum_outerSigCone_dPhi,
      signalChargedHadrCands_sum_outerSigCone_mass,
      signalChargedHadrCands_nTotal_innerSigCone,
      signalChargedHadrCands_nTotal_outerSigCone,
      signalNeutrHadrCands_sum_innerSigCone_pt,
      signalNeutrHadrCands_sum_innerSigCone_dEta,
      signalNeutrHadrCands_sum_innerSigCone_dPhi,
      signalNeutrHadrCands_sum_innerSigCone_mass,
      signalNeutrHadrCands_sum_outerSigCone_pt,
      signalNeutrHadrCands_sum_outerSigCone_dEta,
      signalNeutrHadrCands_sum_outerSigCone_dPhi,
      signalNeutrHadrCands_sum_outerSigCone_mass,
      signalNeutrHadrCands_nTotal_innerSigCone,
      signalNeutrHadrCands_nTotal_outerSigCone,
      signalGammaCands_sum_innerSigCone_pt,
      signalGammaCands_sum_innerSigCone_dEta,
      signalGammaCands_sum_innerSigCone_dPhi,
      signalGammaCands_sum_innerSigCone_mass,
      signalGammaCands_sum_outerSigCone_pt,
      signalGammaCands_sum_outerSigCone_dEta,
      signalGammaCands_sum_outerSigCone_dPhi,
      signalGammaCands_sum_outerSigCone_mass,
      signalGammaCands_nTotal_innerSigCone,
      signalGammaCands_nTotal_outerSigCone,
      isolationChargedHadrCands_sum_pt,
      isolationChargedHadrCands_sum_dEta,
      isolationChargedHadrCands_sum_dPhi,
      isolationChargedHadrCands_sum_mass,
      isolationChargedHadrCands_nTotal,
      isolationNeutrHadrCands_sum_pt,
      isolationNeutrHadrCands_sum_dEta,
      isolationNeutrHadrCands_sum_dPhi,
      isolationNeutrHadrCands_sum_mass,
      isolationNeutrHadrCands_nTotal,
      isolationGammaCands_sum_pt,
      isolationGammaCands_sum_dEta,
      isolationGammaCands_sum_dPhi,
      isolationGammaCands_sum_mass,
      isolationGammaCands_nTotal,
      NumberOfInputs
    };
  };

  namespace dnn_inputs_2017_v2 {
    constexpr int number_of_inner_cell = 11;
    constexpr int number_of_outer_cell = 21;
    constexpr int number_of_conv_features = 64;
    namespace TauBlockInputs {
      enum vars {
        rho = 0,
        tau_pt,
        tau_eta,
        tau_phi,
        tau_mass,
        tau_E_over_pt,
        tau_charge,
        tau_n_charged_prongs,
        tau_n_neutral_prongs,
        chargedIsoPtSum,
        chargedIsoPtSumdR03_over_dR05,
        footprintCorrection,
        neutralIsoPtSum,
        neutralIsoPtSumWeight_over_neutralIsoPtSum,
        neutralIsoPtSumWeightdR03_over_neutralIsoPtSum,
        neutralIsoPtSumdR03_over_dR05,
        photonPtSumOutsideSignalCone,
        puCorrPtSum,
        tau_dxy_pca_x,
        tau_dxy_pca_y,
        tau_dxy_pca_z,
        tau_dxy_valid,
        tau_dxy,
        tau_dxy_sig,
        tau_ip3d_valid,
        tau_ip3d,
        tau_ip3d_sig,
        tau_dz,
        tau_dz_sig_valid,
        tau_dz_sig,
        tau_flightLength_x,
        tau_flightLength_y,
        tau_flightLength_z,
        tau_flightLength_sig,
        tau_pt_weighted_deta_strip,
        tau_pt_weighted_dphi_strip,
        tau_pt_weighted_dr_signal,
        tau_pt_weighted_dr_iso,
        tau_leadingTrackNormChi2,
        tau_e_ratio_valid,
        tau_e_ratio,
        tau_gj_angle_diff_valid,
        tau_gj_angle_diff,
        tau_n_photons,
        tau_emFraction,
        tau_inside_ecal_crack,
        leadChargedCand_etaAtEcalEntrance_minus_tau_eta,
        NumberOfInputs
      };
    }

    namespace EgammaBlockInputs {
      enum vars {
        rho = 0,
        tau_pt,
        tau_eta,
        tau_inside_ecal_crack,
        pfCand_ele_valid,
        pfCand_ele_rel_pt,
        pfCand_ele_deta,
        pfCand_ele_dphi,
        pfCand_ele_pvAssociationQuality,
        pfCand_ele_puppiWeight,
        pfCand_ele_charge,
        pfCand_ele_lostInnerHits,
        pfCand_ele_numberOfPixelHits,
        pfCand_ele_vertex_dx,
        pfCand_ele_vertex_dy,
        pfCand_ele_vertex_dz,
        pfCand_ele_vertex_dx_tauFL,
        pfCand_ele_vertex_dy_tauFL,
        pfCand_ele_vertex_dz_tauFL,
        pfCand_ele_hasTrackDetails,
        pfCand_ele_dxy,
        pfCand_ele_dxy_sig,
        pfCand_ele_dz,
        pfCand_ele_dz_sig,
        pfCand_ele_track_chi2_ndof,
        pfCand_ele_track_ndof,
        ele_valid,
        ele_rel_pt,
        ele_deta,
        ele_dphi,
        ele_cc_valid,
        ele_cc_ele_rel_energy,
        ele_cc_gamma_rel_energy,
        ele_cc_n_gamma,
        ele_rel_trackMomentumAtVtx,
        ele_rel_trackMomentumAtCalo,
        ele_rel_trackMomentumOut,
        ele_rel_trackMomentumAtEleClus,
        ele_rel_trackMomentumAtVtxWithConstraint,
        ele_rel_ecalEnergy,
        ele_ecalEnergy_sig,
        ele_eSuperClusterOverP,
        ele_eSeedClusterOverP,
        ele_eSeedClusterOverPout,
        ele_eEleClusterOverPout,
        ele_deltaEtaSuperClusterTrackAtVtx,
        ele_deltaEtaSeedClusterTrackAtCalo,
        ele_deltaEtaEleClusterTrackAtCalo,
        ele_deltaPhiEleClusterTrackAtCalo,
        ele_deltaPhiSuperClusterTrackAtVtx,
        ele_deltaPhiSeedClusterTrackAtCalo,
        ele_mvaInput_earlyBrem,
        ele_mvaInput_lateBrem,
        ele_mvaInput_sigmaEtaEta,
        ele_mvaInput_hadEnergy,
        ele_mvaInput_deltaEta,
        ele_gsfTrack_normalizedChi2,
        ele_gsfTrack_numberOfValidHits,
        ele_rel_gsfTrack_pt,
        ele_gsfTrack_pt_sig,
        ele_has_closestCtfTrack,
        ele_closestCtfTrack_normalizedChi2,
        ele_closestCtfTrack_numberOfValidHits,
        pfCand_gamma_valid,
        pfCand_gamma_rel_pt,
        pfCand_gamma_deta,
        pfCand_gamma_dphi,
        pfCand_gamma_pvAssociationQuality,
        pfCand_gamma_fromPV,
        pfCand_gamma_puppiWeight,
        pfCand_gamma_puppiWeightNoLep,
        pfCand_gamma_lostInnerHits,
        pfCand_gamma_numberOfPixelHits,
        pfCand_gamma_vertex_dx,
        pfCand_gamma_vertex_dy,
        pfCand_gamma_vertex_dz,
        pfCand_gamma_vertex_dx_tauFL,
        pfCand_gamma_vertex_dy_tauFL,
        pfCand_gamma_vertex_dz_tauFL,
        pfCand_gamma_hasTrackDetails,
        pfCand_gamma_dxy,
        pfCand_gamma_dxy_sig,
        pfCand_gamma_dz,
        pfCand_gamma_dz_sig,
        pfCand_gamma_track_chi2_ndof,
        pfCand_gamma_track_ndof,
        NumberOfInputs
      };
    }

    namespace MuonBlockInputs {
      enum vars {
        rho = 0,
        tau_pt,
        tau_eta,
        tau_inside_ecal_crack,
        pfCand_muon_valid,
        pfCand_muon_rel_pt,
        pfCand_muon_deta,
        pfCand_muon_dphi,
        pfCand_muon_pvAssociationQuality,
        pfCand_muon_fromPV,
        pfCand_muon_puppiWeight,
        pfCand_muon_charge,
        pfCand_muon_lostInnerHits,
        pfCand_muon_numberOfPixelHits,
        pfCand_muon_vertex_dx,
        pfCand_muon_vertex_dy,
        pfCand_muon_vertex_dz,
        pfCand_muon_vertex_dx_tauFL,
        pfCand_muon_vertex_dy_tauFL,
        pfCand_muon_vertex_dz_tauFL,
        pfCand_muon_hasTrackDetails,
        pfCand_muon_dxy,
        pfCand_muon_dxy_sig,
        pfCand_muon_dz,
        pfCand_muon_dz_sig,
        pfCand_muon_track_chi2_ndof,
        pfCand_muon_track_ndof,
        muon_valid,
        muon_rel_pt,
        muon_deta,
        muon_dphi,
        muon_dxy,
        muon_dxy_sig,
        muon_normalizedChi2_valid,
        muon_normalizedChi2,
        muon_numberOfValidHits,
        muon_segmentCompatibility,
        muon_caloCompatibility,
        muon_pfEcalEnergy_valid,
        muon_rel_pfEcalEnergy,
        muon_n_matches_DT_1,
        muon_n_matches_DT_2,
        muon_n_matches_DT_3,
        muon_n_matches_DT_4,
        muon_n_matches_CSC_1,
        muon_n_matches_CSC_2,
        muon_n_matches_CSC_3,
        muon_n_matches_CSC_4,
        muon_n_matches_RPC_1,
        muon_n_matches_RPC_2,
        muon_n_matches_RPC_3,
        muon_n_matches_RPC_4,
        muon_n_hits_DT_1,
        muon_n_hits_DT_2,
        muon_n_hits_DT_3,
        muon_n_hits_DT_4,
        muon_n_hits_CSC_1,
        muon_n_hits_CSC_2,
        muon_n_hits_CSC_3,
        muon_n_hits_CSC_4,
        muon_n_hits_RPC_1,
        muon_n_hits_RPC_2,
        muon_n_hits_RPC_3,
        muon_n_hits_RPC_4,
        NumberOfInputs
      };
    }

    namespace HadronBlockInputs {
      enum vars {
        rho = 0,
        tau_pt,
        tau_eta,
        tau_inside_ecal_crack,
        pfCand_chHad_valid,
        pfCand_chHad_rel_pt,
        pfCand_chHad_deta,
        pfCand_chHad_dphi,
        pfCand_chHad_leadChargedHadrCand,
        pfCand_chHad_pvAssociationQuality,
        pfCand_chHad_fromPV,
        pfCand_chHad_puppiWeight,
        pfCand_chHad_puppiWeightNoLep,
        pfCand_chHad_charge,
        pfCand_chHad_lostInnerHits,
        pfCand_chHad_numberOfPixelHits,
        pfCand_chHad_vertex_dx,
        pfCand_chHad_vertex_dy,
        pfCand_chHad_vertex_dz,
        pfCand_chHad_vertex_dx_tauFL,
        pfCand_chHad_vertex_dy_tauFL,
        pfCand_chHad_vertex_dz_tauFL,
        pfCand_chHad_hasTrackDetails,
        pfCand_chHad_dxy,
        pfCand_chHad_dxy_sig,
        pfCand_chHad_dz,
        pfCand_chHad_dz_sig,
        pfCand_chHad_track_chi2_ndof,
        pfCand_chHad_track_ndof,
        pfCand_chHad_hcalFraction,
        pfCand_chHad_rawCaloFraction,
        pfCand_nHad_valid,
        pfCand_nHad_rel_pt,
        pfCand_nHad_deta,
        pfCand_nHad_dphi,
        pfCand_nHad_puppiWeight,
        pfCand_nHad_puppiWeightNoLep,
        pfCand_nHad_hcalFraction,
        NumberOfInputs
      };
    }
  }  // namespace dnn_inputs_2017_v2

  float getTauID(const pat::Tau& tau, const std::string& tauID, float default_value = -999., bool assert_input = true);

  struct TauFunc {
    const reco::TauDiscriminatorContainer* basicTauDiscriminatorCollection;
    const reco::TauDiscriminatorContainer* basicTauDiscriminatordR03Collection;
    const edm::AssociationVector<reco::PFTauRefProd, std::vector<reco::PFTauTransverseImpactParameterRef>>*
        pfTauTransverseImpactParameters;

    using BasicDiscr = deep_tau::DeepTauBase::BasicDiscriminator;
    std::map<BasicDiscr, size_t> indexMap;
    std::map<BasicDiscr, size_t> indexMapdR03;

    const float getChargedIsoPtSum(const reco::PFTau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatorCollection)[tau_ref].rawValues.at(indexMap.at(BasicDiscr::ChargedIsoPtSum));
    }
    const float getChargedIsoPtSum(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "chargedIsoPtSum");
    }
    const float getChargedIsoPtSumdR03(const reco::PFTau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatordR03Collection)[tau_ref].rawValues.at(indexMapdR03.at(BasicDiscr::ChargedIsoPtSum));
    }
    const float getChargedIsoPtSumdR03(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "chargedIsoPtSumdR03");
    }
    const float getFootprintCorrectiondR03(const reco::PFTau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatordR03Collection)[tau_ref].rawValues.at(
          indexMapdR03.at(BasicDiscr::FootprintCorrection));
    }
    const float getFootprintCorrectiondR03(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "footprintCorrectiondR03");
    }
    const float getNeutralIsoPtSum(const reco::PFTau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatorCollection)[tau_ref].rawValues.at(indexMap.at(BasicDiscr::NeutralIsoPtSum));
    }
    const float getNeutralIsoPtSum(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "neutralIsoPtSum");
    }
    const float getNeutralIsoPtSumdR03(const reco::PFTau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatordR03Collection)[tau_ref].rawValues.at(indexMapdR03.at(BasicDiscr::NeutralIsoPtSum));
    }
    const float getNeutralIsoPtSumdR03(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "neutralIsoPtSumdR03");
    }
    const float getNeutralIsoPtSumWeight(const reco::PFTau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatorCollection)[tau_ref].rawValues.at(indexMap.at(BasicDiscr::NeutralIsoPtSumWeight));
    }
    const float getNeutralIsoPtSumWeight(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "neutralIsoPtSumWeight");
    }
    const float getNeutralIsoPtSumdR03Weight(const reco::PFTau& tau,
                                             const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatordR03Collection)[tau_ref].rawValues.at(
          indexMapdR03.at(BasicDiscr::NeutralIsoPtSumWeight));
    }
    const float getNeutralIsoPtSumdR03Weight(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "neutralIsoPtSumWeightdR03");
    }
    const float getPhotonPtSumOutsideSignalCone(const reco::PFTau& tau,
                                                const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatorCollection)[tau_ref].rawValues.at(
          indexMap.at(BasicDiscr::PhotonPtSumOutsideSignalCone));
    }
    const float getPhotonPtSumOutsideSignalCone(const pat::Tau& tau,
                                                const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "photonPtSumOutsideSignalCone");
    }
    const float getPhotonPtSumOutsideSignalConedR03(const reco::PFTau& tau,
                                                    const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatordR03Collection)[tau_ref].rawValues.at(
          indexMapdR03.at(BasicDiscr::PhotonPtSumOutsideSignalCone));
    }
    const float getPhotonPtSumOutsideSignalConedR03(const pat::Tau& tau,
                                                    const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "photonPtSumOutsideSignalConedR03");
    }
    const float getPuCorrPtSum(const reco::PFTau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return (*basicTauDiscriminatorCollection)[tau_ref].rawValues.at(indexMap.at(BasicDiscr::PUcorrPtSum));
    }
    const float getPuCorrPtSum(const pat::Tau& tau, const edm::RefToBase<reco::BaseTau> tau_ref) const {
      return getTauID(tau, "puCorrPtSum");
    }

    auto getdxyPCA(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->dxy_PCA();
    }
    auto getdxyPCA(const pat::Tau& tau, const size_t tau_index) const { return tau.dxy_PCA(); }
    auto getdxy(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->dxy();
    }
    auto getdxy(const pat::Tau& tau, const size_t tau_index) const { return tau.dxy(); }
    auto getdxyError(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->dxy_error();
    }
    auto getdxyError(const pat::Tau& tau, const size_t tau_index) const { return tau.dxy_error(); }
    auto getdxySig(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->dxy_Sig();
    }
    auto getdxySig(const pat::Tau& tau, const size_t tau_index) const { return tau.dxy_Sig(); }
    auto getip3d(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->ip3d();
    }
    auto getip3d(const pat::Tau& tau, const size_t tau_index) const { return tau.ip3d(); }
    auto getip3dError(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->ip3d_error();
    }
    auto getip3dError(const pat::Tau& tau, const size_t tau_index) const { return tau.ip3d_error(); }
    auto getip3dSig(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->ip3d_Sig();
    }
    auto getip3dSig(const pat::Tau& tau, const size_t tau_index) const { return tau.ip3d_Sig(); }
    auto getHasSecondaryVertex(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->hasSecondaryVertex();
    }
    auto getHasSecondaryVertex(const pat::Tau& tau, const size_t tau_index) const { return tau.hasSecondaryVertex(); }
    auto getFlightLength(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->flightLength();
    }
    auto getFlightLength(const pat::Tau& tau, const size_t tau_index) const { return tau.flightLength(); }
    auto getFlightLengthSig(const reco::PFTau& tau, const size_t tau_index) const {
      return pfTauTransverseImpactParameters->value(tau_index)->flightLengthSig();
    }
    auto getFlightLengthSig(const pat::Tau& tau, const size_t tau_index) const { return tau.flightLengthSig(); }

    auto getLeadingTrackNormChi2(const reco::PFTau& tau) { return reco::tau::lead_track_chi2(tau); }
    auto getLeadingTrackNormChi2(const pat::Tau& tau) { return tau.leadingTrackNormChi2(); }
    auto getEmFraction(const pat::Tau& tau) { return tau.emFraction_MVA(); }
    auto getEmFraction(const reco::PFTau& tau) { return tau.emFraction(); }
    auto getEtaAtEcalEntrance(const pat::Tau& tau) { return tau.etaAtEcalEntranceLeadChargedCand(); }
    auto getEtaAtEcalEntrance(const reco::PFTau& tau) {
      return tau.leadPFChargedHadrCand()->positionAtECALEntrance().eta();
    }
    auto getEcalEnergyLeadingChargedHadr(const reco::PFTau& tau) { return tau.leadPFChargedHadrCand()->ecalEnergy(); }
    auto getEcalEnergyLeadingChargedHadr(const pat::Tau& tau) { return tau.ecalEnergyLeadChargedHadrCand(); }
    auto getHcalEnergyLeadingChargedHadr(const reco::PFTau& tau) { return tau.leadPFChargedHadrCand()->hcalEnergy(); }
    auto getHcalEnergyLeadingChargedHadr(const pat::Tau& tau) { return tau.hcalEnergyLeadChargedHadrCand(); }

    template <typename PreDiscrType>
    bool passPrediscriminants(const PreDiscrType prediscriminants,
                              const size_t andPrediscriminants,
                              const edm::RefToBase<reco::BaseTau> tau_ref) {
      bool passesPrediscriminants = (andPrediscriminants ? 1 : 0);
      // check tau passes prediscriminants
      size_t nPrediscriminants = prediscriminants.size();
      for (size_t iDisc = 0; iDisc < nPrediscriminants; ++iDisc) {
        // current discriminant result for this tau
        double discResult = (*prediscriminants[iDisc].handle)[tau_ref];
        uint8_t thisPasses = (discResult > prediscriminants[iDisc].cut) ? 1 : 0;

        // if we are using the AND option, as soon as one fails,
        // the result is FAIL and we can quit looping.
        // if we are using the OR option as soon as one passes,
        // the result is pass and we can quit looping

        // truth table
        //        |   result (thisPasses)
        //        |     F     |     T
        //-----------------------------------
        // AND(T) | res=fails |  continue
        //        |  break    |
        //-----------------------------------
        // OR (F) |  continue | res=passes
        //        |           |  break

        if (thisPasses ^ andPrediscriminants)  //XOR
        {
          passesPrediscriminants = (andPrediscriminants ? 0 : 1);  //NOR
          break;
        }
      }
      return passesPrediscriminants;
    }
  };

  namespace candFunc {
    inline auto getTauDz(const reco::PFCandidate& cand) { return cand.bestTrack()->dz(); }
    inline auto getTauDz(const pat::PackedCandidate& cand) { return cand.dz(); }
    inline auto getTauDZSigValid(const reco::PFCandidate& cand) {
      return cand.bestTrack() != nullptr && std::isnormal(cand.bestTrack()->dz()) && std::isnormal(cand.dzError()) &&
             cand.dzError() > 0;
    }
    inline auto getTauDZSigValid(const pat::PackedCandidate& cand) {
      return cand.hasTrackDetails() && std::isnormal(cand.dz()) && std::isnormal(cand.dzError()) && cand.dzError() > 0;
    }
    inline auto getTauDxy(const reco::PFCandidate& cand) { return cand.bestTrack()->dxy(); }
    inline auto getTauDxy(const pat::PackedCandidate& cand) { return cand.dxy(); }
    inline auto getPvAssocationQuality(const reco::PFCandidate& cand) { return 0.7013f; }
    inline auto getPvAssocationQuality(const pat::PackedCandidate& cand) { return cand.pvAssociationQuality(); }
    inline auto getPuppiWeight(const reco::PFCandidate& cand, const float aod_value) { return aod_value; }
    inline auto getPuppiWeight(const pat::PackedCandidate& cand, const float aod_value) { return cand.puppiWeight(); }
    inline auto getPuppiWeightNoLep(const reco::PFCandidate& cand, const float aod_value) { return aod_value; }
    inline auto getPuppiWeightNoLep(const pat::PackedCandidate& cand, const float aod_value) {
      return cand.puppiWeightNoLep();
    }
    inline auto getLostInnerHits(const reco::PFCandidate& cand, float default_value) {
      return cand.bestTrack() != nullptr
                 ? cand.bestTrack()->hitPattern().numberOfLostHits(reco::HitPattern::MISSING_INNER_HITS)
                 : default_value;
    }
    inline auto getLostInnerHits(const pat::PackedCandidate& cand, float default_value) { return cand.lostInnerHits(); }
    inline auto getNumberOfPixelHits(const reco::PFCandidate& cand, float default_value) {
      return cand.bestTrack() != nullptr
                 ? cand.bestTrack()->hitPattern().numberOfLostHits(reco::HitPattern::MISSING_INNER_HITS)
                 : default_value;
    }
    inline auto getNumberOfPixelHits(const pat::PackedCandidate& cand, float default_value) {
      return cand.numberOfPixelHits();
    }
    inline auto getHasTrackDetails(const reco::PFCandidate& cand) { return cand.bestTrack() != nullptr; }
    inline auto getHasTrackDetails(const pat::PackedCandidate& cand) { return cand.hasTrackDetails(); }
    inline auto getPseudoTrack(const reco::PFCandidate& cand) { return *cand.bestTrack(); }
    inline auto getPseudoTrack(const pat::PackedCandidate& cand) { return cand.pseudoTrack(); }
    inline auto getFromPV(const reco::PFCandidate& cand) { return 0.9994f; }
    inline auto getFromPV(const pat::PackedCandidate& cand) { return cand.fromPV(); }
    inline auto getHCalFraction(const reco::PFCandidate& cand, bool disable_hcalFraction_workaround) {
      return cand.rawHcalEnergy() / (cand.rawHcalEnergy() + cand.rawEcalEnergy());
    }
    inline auto getHCalFraction(const pat::PackedCandidate& cand, bool disable_hcalFraction_workaround) {
      float hcal_fraction = 0.;
      if (disable_hcalFraction_workaround) {
        // CV: use consistent definition for pfCand_chHad_hcalFraction
        //     in DeepTauId.cc code and in TauMLTools/Production/plugins/TauTupleProducer.cc
        hcal_fraction = cand.hcalFraction();
      } else {
        // CV: backwards compatibility with DeepTau training v2p1 used during Run 2
        if (cand.pdgId() == 1 || cand.pdgId() == 130) {
          hcal_fraction = cand.hcalFraction();
        } else if (cand.isIsolatedChargedHadron()) {
          hcal_fraction = cand.rawHcalFraction();
        }
      }
      return hcal_fraction;
    }
    inline auto getRawCaloFraction(const reco::PFCandidate& cand) {
      return (cand.rawEcalEnergy() + cand.rawHcalEnergy()) / cand.energy();
    }
    inline auto getRawCaloFraction(const pat::PackedCandidate& cand) { return cand.rawCaloFraction(); }
  };  // namespace candFunc

  template <typename LVector1, typename LVector2>
  float dEta(const LVector1& p4, const LVector2& tau_p4) {
    return static_cast<float>(p4.eta() - tau_p4.eta());
  }

  template <typename LVector1, typename LVector2>
  float dPhi(const LVector1& p4_1, const LVector2& p4_2) {
    return static_cast<float>(reco::deltaPhi(p4_2.phi(), p4_1.phi()));
  }

  struct MuonHitMatchV1 {
    static constexpr int n_muon_stations = 4;

    std::map<int, std::vector<UInt_t>> n_matches, n_hits;
    unsigned n_muons{0};
    const pat::Muon* best_matched_muon{nullptr};
    double deltaR2_best_match{-1};

    MuonHitMatchV1() {
      n_matches[MuonSubdetId::DT].assign(n_muon_stations, 0);
      n_matches[MuonSubdetId::CSC].assign(n_muon_stations, 0);
      n_matches[MuonSubdetId::RPC].assign(n_muon_stations, 0);
      n_hits[MuonSubdetId::DT].assign(n_muon_stations, 0);
      n_hits[MuonSubdetId::CSC].assign(n_muon_stations, 0);
      n_hits[MuonSubdetId::RPC].assign(n_muon_stations, 0);
    }

    void addMatchedMuon(const pat::Muon& muon, reco::BaseTau const& tau);

    template <typename TauCastType>
    std::vector<const pat::Muon*> findMatchedMuons(const TauCastType& tau,
                                                   const std::vector<pat::Muon>* muons,
                                                   double deltaR,
                                                   double minPt) {
      const reco::Muon* hadr_cand_muon = nullptr;
      if (tau.leadPFChargedHadrCand().isNonnull() && tau.leadPFChargedHadrCand()->muonRef().isNonnull())
        hadr_cand_muon = tau.leadPFChargedHadrCand()->muonRef().get();
      std::vector<const pat::Muon*> matched_muons;
      const double dR2 = deltaR * deltaR;
      for (const pat::Muon& muon : *muons) {
        const reco::Muon* reco_muon = &muon;
        if (muon.pt() <= minPt)
          continue;
        if (reco_muon == hadr_cand_muon)
          continue;
        if (reco::deltaR2(tau.p4(), muon.p4()) >= dR2)
          continue;
        matched_muons.push_back(&muon);
      }
      return matched_muons;
    }

    template <typename dnn, typename TensorElemGet, typename TauCastType>
    void fillTensor(const TensorElemGet& get, const TauCastType& tau, float default_value) const {
      get(dnn::n_matched_muons) = n_muons;
      get(dnn::muon_pt) = best_matched_muon != nullptr ? best_matched_muon->p4().pt() : default_value;
      get(dnn::muon_dEta) = best_matched_muon != nullptr ? dEta(best_matched_muon->p4(), tau.p4()) : default_value;
      get(dnn::muon_dPhi) = best_matched_muon != nullptr ? dPhi(best_matched_muon->p4(), tau.p4()) : default_value;
      get(dnn::muon_n_matches_DT_1) = n_matches.at(MuonSubdetId::DT).at(0);
      get(dnn::muon_n_matches_DT_2) = n_matches.at(MuonSubdetId::DT).at(1);
      get(dnn::muon_n_matches_DT_3) = n_matches.at(MuonSubdetId::DT).at(2);
      get(dnn::muon_n_matches_DT_4) = n_matches.at(MuonSubdetId::DT).at(3);
      get(dnn::muon_n_matches_CSC_1) = n_matches.at(MuonSubdetId::CSC).at(0);
      get(dnn::muon_n_matches_CSC_2) = n_matches.at(MuonSubdetId::CSC).at(1);
      get(dnn::muon_n_matches_CSC_3) = n_matches.at(MuonSubdetId::CSC).at(2);
      get(dnn::muon_n_matches_CSC_4) = n_matches.at(MuonSubdetId::CSC).at(3);
      get(dnn::muon_n_hits_DT_2) = n_hits.at(MuonSubdetId::DT).at(1);
      get(dnn::muon_n_hits_DT_3) = n_hits.at(MuonSubdetId::DT).at(2);
      get(dnn::muon_n_hits_DT_4) = n_hits.at(MuonSubdetId::DT).at(3);
      get(dnn::muon_n_hits_CSC_2) = n_hits.at(MuonSubdetId::CSC).at(1);
      get(dnn::muon_n_hits_CSC_3) = n_hits.at(MuonSubdetId::CSC).at(2);
      get(dnn::muon_n_hits_CSC_4) = n_hits.at(MuonSubdetId::CSC).at(3);
      get(dnn::muon_n_hits_RPC_2) = n_hits.at(MuonSubdetId::RPC).at(1);
      get(dnn::muon_n_hits_RPC_3) = n_hits.at(MuonSubdetId::RPC).at(2);
      get(dnn::muon_n_hits_RPC_4) = n_hits.at(MuonSubdetId::RPC).at(3);
      get(dnn::muon_n_stations_with_matches_03) = countMuonStationsWithMatches(0, 3);
      get(dnn::muon_n_stations_with_hits_23) = countMuonStationsWithHits(2, 3);
    }

  private:
    unsigned countMuonStationsWithMatches(size_t first_station, size_t last_station) const;

    unsigned countMuonStationsWithHits(size_t first_station, size_t last_station) const;
  };

  struct MuonHitMatchV2 {
    static constexpr size_t n_muon_stations = 4;
    static constexpr int first_station_id = 1;
    static constexpr int last_station_id = first_station_id + n_muon_stations - 1;
    using CountArray = std::array<unsigned, n_muon_stations>;
    using CountMap = std::map<int, CountArray>;

    const std::vector<int>& consideredSubdets() {
      static const std::vector<int> subdets = {MuonSubdetId::DT, MuonSubdetId::CSC, MuonSubdetId::RPC};
      return subdets;
    }

    const std::string& subdetName(int subdet) {
      static const std::map<int, std::string> subdet_names = {
          {MuonSubdetId::DT, "DT"}, {MuonSubdetId::CSC, "CSC"}, {MuonSubdetId::RPC, "RPC"}};
      if (!subdet_names.count(subdet))
        throw cms::Exception("MuonHitMatch") << "Subdet name for subdet id " << subdet << " not found.";
      return subdet_names.at(subdet);
    }

    size_t getStationIndex(int station, bool throw_exception) const {
      if (station < first_station_id || station > last_station_id) {
        if (throw_exception)
          throw cms::Exception("MuonHitMatch") << "Station id is out of range";
        return std::numeric_limits<size_t>::max();
      }
      return static_cast<size_t>(station - 1);
    }

    MuonHitMatchV2(const pat::Muon& muon) {
      for (int subdet : consideredSubdets()) {
        n_matches[subdet].fill(0);
        n_hits[subdet].fill(0);
      }

      countMatches(muon, n_matches);
      countHits(muon, n_hits);
    }

    void countMatches(const pat::Muon& muon, CountMap& n_matches) {
      for (const auto& segment : muon.matches()) {
        if (segment.segmentMatches.empty() && segment.rpcMatches.empty())
          continue;
        if (n_matches.count(segment.detector())) {
          const size_t station_index = getStationIndex(segment.station(), true);
          ++n_matches.at(segment.detector()).at(station_index);
        }
      }
    }

    void countHits(const pat::Muon& muon, CountMap& n_hits) {
      if (muon.outerTrack().isNonnull()) {
        const auto& hit_pattern = muon.outerTrack()->hitPattern();
        for (int hit_index = 0; hit_index < hit_pattern.numberOfAllHits(reco::HitPattern::TRACK_HITS); ++hit_index) {
          auto hit_id = hit_pattern.getHitPattern(reco::HitPattern::TRACK_HITS, hit_index);
          if (hit_id == 0)
            break;
          if (hit_pattern.muonHitFilter(hit_id) && (hit_pattern.getHitType(hit_id) == TrackingRecHit::valid ||
                                                    hit_pattern.getHitType(hit_id) == TrackingRecHit::bad)) {
            const size_t station_index = getStationIndex(hit_pattern.getMuonStation(hit_id), false);
            if (station_index < n_muon_stations) {
              CountArray* muon_n_hits = nullptr;
              if (hit_pattern.muonDTHitFilter(hit_id))
                muon_n_hits = &n_hits.at(MuonSubdetId::DT);
              else if (hit_pattern.muonCSCHitFilter(hit_id))
                muon_n_hits = &n_hits.at(MuonSubdetId::CSC);
              else if (hit_pattern.muonRPCHitFilter(hit_id))
                muon_n_hits = &n_hits.at(MuonSubdetId::RPC);

              if (muon_n_hits)
                ++muon_n_hits->at(station_index);
            }
          }
        }
      }
    }

    unsigned nMatches(int subdet, int station) const {
      if (!n_matches.count(subdet))
        throw cms::Exception("MuonHitMatch") << "Subdet " << subdet << " not found.";
      const size_t station_index = getStationIndex(station, true);
      return n_matches.at(subdet).at(station_index);
    }

    unsigned nHits(int subdet, int station) const {
      if (!n_hits.count(subdet))
        throw cms::Exception("MuonHitMatch") << "Subdet " << subdet << " not found.";
      const size_t station_index = getStationIndex(station, true);
      return n_hits.at(subdet).at(station_index);
    }

    unsigned countMuonStationsWithMatches(int first_station, int last_station) const {
      static const std::map<int, std::vector<bool>> masks = {
          {MuonSubdetId::DT, {false, false, false, false}},
          {MuonSubdetId::CSC, {true, false, false, false}},
          {MuonSubdetId::RPC, {false, false, false, false}},
      };
      const size_t first_station_index = getStationIndex(first_station, true);
      const size_t last_station_index = getStationIndex(last_station, true);
      unsigned cnt = 0;
      for (size_t n = first_station_index; n <= last_station_index; ++n) {
        for (const auto& match : n_matches) {
          if (!masks.at(match.first).at(n) && match.second.at(n) > 0)
            ++cnt;
        }
      }
      return cnt;
    }

    unsigned countMuonStationsWithHits(int first_station, int last_station) const {
      static const std::map<int, std::vector<bool>> masks = {
          {MuonSubdetId::DT, {false, false, false, false}},
          {MuonSubdetId::CSC, {false, false, false, false}},
          {MuonSubdetId::RPC, {false, false, false, false}},
      };

      const size_t first_station_index = getStationIndex(first_station, true);
      const size_t last_station_index = getStationIndex(last_station, true);
      unsigned cnt = 0;
      for (size_t n = first_station_index; n <= last_station_index; ++n) {
        for (const auto& hit : n_hits) {
          if (!masks.at(hit.first).at(n) && hit.second.at(n) > 0)
            ++cnt;
        }
      }
      return cnt;
    }

  private:
    CountMap n_matches, n_hits;
  };

  enum class CellObjectType {
    PfCand_electron,
    PfCand_muon,
    PfCand_chargedHadron,
    PfCand_neutralHadron,
    PfCand_gamma,
    Electron,
    Muon,
    Other
  };

  template <typename Object>
  CellObjectType GetCellObjectType(const Object&);
  template <>
  CellObjectType GetCellObjectType(const pat::Electron&);
  template <>
  CellObjectType GetCellObjectType(const pat::Muon&);
  template <>
  CellObjectType GetCellObjectType(reco::Candidate const& cand);

  using Cell = std::map<CellObjectType, size_t>;
  struct CellIndex {
    int eta, phi;

    bool operator<(const CellIndex& other) const {
      if (eta != other.eta)
        return eta < other.eta;
      return phi < other.phi;
    }
  };

  class CellGrid {
  public:
    using Map = std::map<CellIndex, Cell>;
    using const_iterator = Map::const_iterator;

    CellGrid(unsigned n_cells_eta,
             unsigned n_cells_phi,
             double cell_size_eta,
             double cell_size_phi,
             bool disable_CellIndex_workaround)
        : nCellsEta(n_cells_eta),
          nCellsPhi(n_cells_phi),
          nTotal(nCellsEta * nCellsPhi),
          cellSizeEta(cell_size_eta),
          cellSizePhi(cell_size_phi),
          disable_CellIndex_workaround_(disable_CellIndex_workaround) {
      if (nCellsEta % 2 != 1 || nCellsEta < 1)
        throw cms::Exception("DeepTauId") << "Invalid number of eta cells.";
      if (nCellsPhi % 2 != 1 || nCellsPhi < 1)
        throw cms::Exception("DeepTauId") << "Invalid number of phi cells.";
      if (cellSizeEta <= 0 || cellSizePhi <= 0)
        throw cms::Exception("DeepTauId") << "Invalid cell size.";
    }

    int maxEtaIndex() const { return static_cast<int>((nCellsEta - 1) / 2); }
    int maxPhiIndex() const { return static_cast<int>((nCellsPhi - 1) / 2); }
    double maxDeltaEta() const { return cellSizeEta * (0.5 + maxEtaIndex()); }
    double maxDeltaPhi() const { return cellSizePhi * (0.5 + maxPhiIndex()); }
    int getEtaTensorIndex(const CellIndex& cellIndex) const { return cellIndex.eta + maxEtaIndex(); }
    int getPhiTensorIndex(const CellIndex& cellIndex) const { return cellIndex.phi + maxPhiIndex(); }

    bool tryGetCellIndex(double deltaEta, double deltaPhi, CellIndex& cellIndex) const;

    size_t num_valid_cells() const { return cells.size(); }
    Cell& operator[](const CellIndex& cellIndex) { return cells[cellIndex]; }
    const Cell& at(const CellIndex& cellIndex) const { return cells.at(cellIndex); }
    size_t count(const CellIndex& cellIndex) const { return cells.count(cellIndex); }
    const_iterator find(const CellIndex& cellIndex) const { return cells.find(cellIndex); }
    const_iterator begin() const { return cells.begin(); }
    const_iterator end() const { return cells.end(); }

  public:
    const unsigned nCellsEta, nCellsPhi, nTotal;
    const double cellSizeEta, cellSizePhi;

  private:
    std::map<CellIndex, Cell> cells;
    const bool disable_CellIndex_workaround_;
  };
}  // namespace deep_tau_2017

#endif
