#ifndef _COBJECTLIST
#define _COBJECTLIST

#include "types.h"

#include "MetroidPrime/TGameTypes.hpp"

#define kMaxObjects 1024

enum EGameObjectList {
  kOL_Invalid = -1,
  kOL_All,
  kOL_Actor,
  kOL_PhysicsActor,
  kOL_GameCamera,
  kOL_GameLight,
  kOL_ListeningAi,
  kOL_AiWaypoint,
  kOL_PlatformAndDoor,
};

class CEntity;
class CObjectList {
  struct SObjectListEntry {
    CEntity* mEnt;
    short mNext;
    short mPrev;
    SObjectListEntry() : mEnt(nullptr), mNext(-1), mPrev(-1) {}
  };

public:
  CObjectList(EGameObjectList list);

  virtual uchar IsQualified(const CEntity& ent);

  void AddObject(CEntity& ent);
  void RemoveObject(TUniqueId uid);
  CEntity* GetObjectById(TUniqueId uid);
  const CEntity* GetObjectById(TUniqueId uid) const;
  CEntity* GetValidObjectById(TUniqueId uid);
  const CEntity* GetValidObjectById(TUniqueId uid) const;
  CEntity* operator[](int idx);
  const CEntity* operator[](int idx) const;
  const CEntity* GetObjectByIndex(int idx) const;
  int size() const { return mCount; }

  int GetFirstObjectIndex() const { return mFirstId; }
  int GetNextObjectIndex(int idx) const {
    if (idx != -1) {
      return mObjects[idx].mNext;
    } else {
      return -1;
    }
  }

#ifdef TARGET_XBOX
  // PORT_XBOX: the entity `ahead` links after idx asked for in the cache
  // (its first line, the line with its flags at 0x30, and `off` if not 0).
  // The state manager's walks over every object wait on each entity's
  // lines, one miss after another (round hw9 Main Plaza: ~4% of the CPU in
  // PreThink, Think and the sorted-list update). A prefetch is only a hint.
  void PortPrefetchAhead(int idx, int ahead, int off) const {
    for (; ahead > 0 && idx != -1; --ahead) {
      idx = mObjects[idx].mNext;
    }
    if (idx != -1 && mObjects[idx].mEnt != nullptr) {
      const char* p = reinterpret_cast< const char* >(mObjects[idx].mEnt);
      __builtin_prefetch(p);
      __builtin_prefetch(p + 0x30);
      if (off != 0) {
        __builtin_prefetch(p + off);
      }
    }
  }
#endif

private:
  SObjectListEntry mObjects[1024];
  EGameObjectList mListType;
  short mFirstId;
  short mCount;
};
CHECK_SIZEOF(CObjectList, 0x200c)

#endif // _COBJECTLIST
