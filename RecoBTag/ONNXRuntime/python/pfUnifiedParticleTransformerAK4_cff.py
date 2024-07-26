import FWCore.ParameterSet.Config as cms

from RecoBTag.FeatureTools.pfUnifiedParticleTransformerAK4TagInfos_cfi import pfUnifiedParticleTransformerAK4TagInfos

from RecoBTag.ONNXRuntime.pfUnifiedParticleTransformerAK4JetTags_cfi import pfUnifiedParticleTransformerAK4JetTags
from RecoBTag.ONNXRuntime.pfUnifiedParticleTransformerAK4DiscriminatorsJetTags_cfi import pfUnifiedParticleTransformerAK4DiscriminatorsJetTags
from CommonTools.PileupAlgos.Puppi_cff import puppi
from CommonTools.RecoAlgos.primaryVertexAssociation_cfi import primaryVertexAssociation
from RecoBTag.ONNXRuntime.pfUnifiedParticleTransformerAK4SonicJetTags_cfi import pfUnifiedParticleTransformerAK4SonicJetTags as _pfUnifiedParticleTransformerAK4SonicJetTags
from Configuration.ProcessModifiers.unifiedparticleTransformerAK4SonicTriton_cff import unifiedparticleTransformerAK4SonicTriton

unifiedparticleTransformerAK4SonicTriton.toReplaceWith(pfUnifiedParticleTransformerAK4JetTags, _pfUnifiedParticleTransformerAK4SonicJetTags.clone(
    Client = cms.PSet(
        timeout = cms.untracked.uint32(300),
        mode = cms.string("Async"),
        modelName = cms.string("unifiedparticletransformer_AK4"), # double check
        modelConfigPath = cms.FileInPath("RecoBTag/Combined/data/models/unifiedparticletransformer_AK4/config.pbtxt"), # this is SONIC, not currently in the CMSSW, so the models/ will be copied to this location privately
        modelVersion = cms.string(""),
        verbose = cms.untracked.bool(False),
        allowedTries = cms.untracked.uint32(0),
        useSharedMemory = cms.untracked.bool(True),
        compression = cms.untracked.string(""),
    ),
    flav_names = pfUnifiedParticleTransformerAK4JetTags.flav_names,
))


# declare all the discriminators
# probs
_pfUnifiedParticleTransformerAK4JetTagsProbs = ['pfUnifiedParticleTransformerAK4JetTags:' + flav_name
                                 for flav_name in pfUnifiedParticleTransformerAK4JetTags.flav_names]
# meta-taggers
_pfUnifiedParticleTransformerAK4JetTagsMetaDiscrs = ['pfUnifiedParticleTransformerAK4DiscriminatorsJetTags:' + disc.name.value()
                                      for disc in pfUnifiedParticleTransformerAK4DiscriminatorsJetTags.discriminators]
_pfUnifiedParticleTransformerAK4JetTagsAll = _pfUnifiedParticleTransformerAK4JetTagsProbs + _pfUnifiedParticleTransformerAK4JetTagsMetaDiscrs



# ==
# This task is not used, useful only if we run it from RECO jets (RECO/AOD)
pfUnifiedParticleTransformerAK4Task = cms.Task(puppi, primaryVertexAssociation,
                             pfUnifiedParticleTransformerAK4TagInfos, pfUnifiedParticleTransformerAK4JetTags,
                             pfUnifiedParticleTransformerAK4DiscriminatorsJetTags)
# run from MiniAOD instead
pfUnifiedParticleTransformerAK4FromMiniAODTask = cms.Task(pfUnifiedParticleTransformerAK4TagInfos,
                             pfUnifiedParticleTransformerAK4JetTags,
                             pfUnifiedParticleTransformerAK4DiscriminatorsJetTags)
