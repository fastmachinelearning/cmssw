import FWCore.ParameterSet.Config as cms

sonic_deeptau = cms.EDProducer("DeepTauIdSonicProducer",
    Client = cms.PSet(
        timeout = cms.untracked.uint32(300),
        modelName = cms.string("deeptau_ensemble"),
        mode = cms.string("Async"),
        modelConfigPath = cms.FileInPath("HeterogeneousCore/SonicTriton/data/models/deeptau_ensemble/config.pbtxt"),
        modelVersion = cms.string(""),
        verbose = cms.untracked.bool(True),
        allowedTries = cms.untracked.uint32(0),
    ),
    electrons = cms.InputTag('slimmedElectrons'),
    muons = cms.InputTag('slimmedMuons'),
    taus = cms.InputTag('slimmedTaus'),
    pfcands = cms.InputTag('packedPFCandidates'),
    vertices = cms.InputTag('offlineSlimmedPrimaryVertices'),
    rho = cms.InputTag('fixedGridRhoAll'),
    disable_dxy_pca = cms.bool(True)
)
