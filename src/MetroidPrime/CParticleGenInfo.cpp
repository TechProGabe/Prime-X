#include "MetroidPrime/CParticleGenInfo.hpp"

#include "MetroidPrime/CGameLight.hpp"
#include "MetroidPrime/CStateManager.hpp"

#include "Kyoto/Particles/CParticleGen.hpp"
#ifdef TARGET_XBOX
#include "Kyoto/Animation/CCharLayoutInfo.hpp"
#endif

CParticleGenInfo::CParticleGenInfo(const SObjectTag& part, int frameCount,
                                   const rstl::string& boneName, const CVector3f& scale,
                                   CParticleData::EParentedMode parentMode, int flags,
                                   EParticleGenType type)
: x4_part(part)
, xc_seconds(frameCount * (1.0f / 60.f))
, x10_boneName(boneName)
, x20_curTime(0.f)
, x24_active(false)
, x28_parentMode(parentMode)
, x2c_flags(flags)
, x30_particleScale(scale)
, x3c_finishTime(0.f)
, x40_grabInitialData(false)
, x44_transform(CTransform4f::Identity())
, x74_offset(0.f, 0.f, 0.f)
, x80_type(type)
#ifdef TARGET_XBOX
, xPortSegLayout(nullptr)
, xPortSeg(CSegId::Invalid())
#endif
{
}

#ifdef TARGET_XBOX
CSegId CParticleGenInfo::PortLocatorSeg(const CCharLayoutInfo& layout) {
  if (xPortSegLayout != &layout) {
    xPortSeg = layout.GetSegIdFromString(x10_boneName);
    xPortSegLayout = &layout;
  }
  return xPortSeg;
}
#endif

TUniqueId _initializeLight(const rstl::ncrc_ptr< CParticleGen >& system, CStateManager& stateMgr,
                           TAreaId areaId, int lightId) {
  if (system->SystemHasLight()) {
    TUniqueId ret = stateMgr.AllocateUniqueId();
    stateMgr.AddObject(rs_new CGameLight(
        ret, areaId, false, rstl::string_l("ParticleLight"),
        CTransform4f(system->GetOrientation().BuildMatrix3f(), system->GetTranslation()),
        kInvalidUniqueId, system->GetLight(), lightId, 0, 0.f));

    return ret;
  } else {
    return kInvalidUniqueId;
  }
}
