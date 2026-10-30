#include "Kyoto/Animation/CHierarchyPoseBuilder.hpp"

#include "Kyoto/Animation/CCharLayoutInfo.hpp"
#include "Kyoto/Animation/CPoseAsTransforms.hpp"
#include "rstl/reserved_vector.hpp"
#ifdef TARGET_XBOX
#include <string.h> // PORT_XBOX: memcpy (BuildTransform's chain cache)
#endif

#ifdef TARGET_XBOX
// PORT_XBOX: BuildTransform walks a segment's chain from the root, a
// quaternion product and a matrix a link, for every locator asked for
// (CAnimData::GetLocatorTransform: weapons, head tracking, effects; 0.8% of
// xemu's pirate room). Each link's running state (rotation, offset,
// matrix) is kept by builder, tree version and segment, and a later walk
// resumes from the deepest one kept: the same operations in the same
// order, so the same bits. Direct-mapped; a collision only misses.
namespace {
struct SPortChainLink {
  const CHierarchyPoseBuilder* builder;
  uint version;
  float scale;
  uchar seg;
  uchar rotation[sizeof(CQuaternion)];   // the running state, as bytes (no default constructors)
  uchar offset[sizeof(CVector3f)];
  uchar matrix[sizeof(CMatrix3f)];
};
const int kPortChainLinks = 512;
SPortChainLink sPortChain[kPortChainLinks];
uint sPortVersion;

inline SPortChainLink& PortChainLink(const CHierarchyPoseBuilder* b, uint version, uchar seg) {
  uint h = static_cast< uint >(reinterpret_cast< uintptr_t >(b)) * 2654435761u ^ version * 40503u ^
           static_cast< uint >(seg) * 2246822519u;
  return sPortChain[(h >> 7) & (kPortChainLinks - 1)];
}
} // namespace

uint CHierarchyPoseBuilder::PortNextVersion() { return ++sPortVersion; }
#endif

CHierarchyPoseBuilder::CHierarchyPoseBuilder(const CLayoutDescription& layout)
: x0_layoutDesc(layout), x38_treeMap(layout.GetNumSegments()) {
#ifdef TARGET_XBOX
  xPortVersion = PortNextVersion();
#endif
  TToken< CCharLayoutInfo > layoutToken = layout.ScaledLayout();
  const CCharLayoutInfo& layoutInfo = **layoutToken;
  const CSegIdList& segments = layoutInfo.GetBodyPartSegIds();
  for (CSegIdList::const_iterator it = segments.begin(), end = segments.end(); it != end; ++it) {
    CSegId seg = *it;
    BuildIntoHeirarchy(layoutInfo, seg, CSegId::Character());
  }
}

void CHierarchyPoseBuilder::BuildIntoHeirarchy(const CCharLayoutInfo& layout, const CSegId& seg,
                                               const CSegId& root) {
  const CSegId id = seg;
  if (!x38_treeMap.ContainsDataFor(id)) {
    CSegId parent = layout.GetOriginalParent(seg);
    if (parent == root) {
      x30_rootId.build(seg);
      x38_treeMap.insert(seg, CTreeNode(CSegId::Null(), CSegId::Null(),
                                     layout.GetFromParentUnrotated(seg)));
    } else {
      BuildIntoHeirarchy(layout, parent, root);
      x38_treeMap.insert(seg, x38_treeMap[parent].NodeForNextChildInserted(
                                  seg, CSegId::Null(), layout.GetFromParentUnrotated(seg)));
    }
  }
}

void CHierarchyPoseBuilder::BuildNoScale(CPoseAsTransforms& pose) {
  CSegId root = *x30_rootId;
  pose.Clear();
  RecursivelyBuildNoScale(root, x38_treeMap[root], pose, CQuaternion::NoRotation(),
                          CMatrix3f::Identity(), CVector3f::Zero());
}

void CHierarchyPoseBuilder::RecursivelyBuild(const CSegId& seg, const CTreeNode& node,
                                             CPoseAsTransforms& pose, const CQuaternion& rotation,
                                             const CMatrix3f& matrix,
                                             const CVector3f& offset) const {
  const CQuaternion& nodeRotation = node.GetRotation();
  CQuaternion childRotation = rotation * nodeRotation;
  float scale = x0_layoutDesc.GlobalScale();
  CMatrix3f childMatrix = scale == 1.f
                              ? childRotation.BuildTransform()
                              : matrix * (nodeRotation.BuildTransform() * CMatrix3f::Scale(scale));
  CVector3f childOffset = offset + matrix * node.GetOffset();
  pose.Insert(seg, childMatrix, childOffset);

#ifdef TARGET_XBOX
  // PORT_XBOX: the children's matrix once, not per child (and at scale 1 it
  // is childMatrix itself): the same function of the same quaternion
  CSegId child = node.GetFirstChildSegment();
  if (child == CSegId::Null()) {
    return;
  }
  const CMatrix3f childRotMatrix = scale == 1.f ? childMatrix : childRotation.BuildTransform();
  while (child != CSegId::Null()) {
    const CSegId childId = child;
    const CTreeNode& childNode = x38_treeMap[childId];
    RecursivelyBuild(childId, childNode, pose, childRotation, childRotMatrix, childOffset);
    child = childNode.GetNextSiblingSegment();
  }
#else
  CSegId child = node.GetFirstChildSegment();
  while (child != CSegId::Null()) {
    const CSegId childId = child;
    const CTreeNode& childNode = x38_treeMap[childId];
    RecursivelyBuild(childId, childNode, pose, childRotation, childRotation.BuildTransform(),
                     childOffset);
    child = childNode.GetNextSiblingSegment();
  }
#endif
}

void CHierarchyPoseBuilder::RecursivelyBuildNoScale(const CSegId& seg, const CTreeNode& node,
                                                    CPoseAsTransforms& pose,
                                                    const CQuaternion& rotation,
                                                    const CMatrix3f& matrix,
                                                    const CVector3f& offset) const {
  CQuaternion childRotation = rotation * node.GetRotation();
  CMatrix3f childMatrix = childRotation.BuildTransform();
  CVector3f childOffset = offset + matrix * node.GetOffset();
  pose.Insert(seg, childMatrix, childOffset);

  CSegId child = node.GetFirstChildSegment();
  while (child != CSegId::Null()) {
    const CSegId childId = child;
    const CTreeNode& childNode = x38_treeMap[childId];
    RecursivelyBuild(childId, childNode, pose, childRotation, childMatrix, childOffset);
    child = childNode.GetNextSiblingSegment();
  }
}

void CHierarchyPoseBuilder::BuildTransform(const CSegId& seg, CTransform4f& transform) const {
  rstl::reserved_vector< CSegId, 100 > segments;
  CSegId current = seg;
  const CCharLayoutInfo& layout = **CharLayoutInfo();
  float scale = x0_layoutDesc.GlobalScale();
  while (current != CSegId::Character()) {
    segments.push_back(current);
    current = layout.GetOriginalParent(current);
  }

  CQuaternion rotation = CQuaternion::NoRotation();
  CVector3f offset(0.f, 0.f, 0.f);
  CMatrix3f matrix = CMatrix3f::Identity();
#ifdef TARGET_XBOX
  // PORT_XBOX: start below the deepest link already walked (see above)
  int from = segments.size();
  for (int j = 0; j < segments.size(); ++j) {
    const SPortChainLink& l = PortChainLink(this, xPortVersion, segments[j].val());
    if (l.builder == this && l.version == xPortVersion && l.seg == segments[j].val() && l.scale == scale) {
      memcpy(&rotation, l.rotation, sizeof rotation);
      memcpy(&offset, l.offset, sizeof offset);
      memcpy(&matrix, l.matrix, sizeof matrix);
      from = j;
      break;
    }
  }
  for (AUTO(it, segments.begin() + from); it != segments.begin(); --it) {
#else
  for (AUTO(it, segments.end()); it != segments.begin(); --it) {
#endif
    const CTreeNode& node = x38_treeMap[*(it - 1)];
    const CQuaternion& nodeRotation = node.GetRotation();
    rotation = rotation * nodeRotation;
    offset += matrix * node.GetOffset();
    matrix = scale == 1.f ? rotation.BuildTransform()
                          : matrix * (nodeRotation.BuildTransform() * CMatrix3f::Scale(scale));
#ifdef TARGET_XBOX
    SPortChainLink& l = PortChainLink(this, xPortVersion, (it - 1)->val());
    l.builder = this;
    l.version = xPortVersion;
    l.scale = scale;
    l.seg = (it - 1)->val();
    memcpy(l.rotation, &rotation, sizeof rotation);
    memcpy(l.offset, &offset, sizeof offset);
    memcpy(l.matrix, &matrix, sizeof matrix);
#endif
  }
  transform.SetRotation(matrix);
  transform.SetTranslation(offset);
}

uchar CLayoutDescription::GetNumSegments() const {
  return x0_layoutToken->GetBodyPartSegIds().size();
}
