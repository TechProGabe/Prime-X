// spike: CHECK_SIZEOF on, as the layout oracle
#pragma once
#include <stddef.h>
#define PX_CAT2(a, b) a##b
#define PX_CAT(a, b) PX_CAT2(a, b)
#define CHECK_SIZEOF(cls, size) static_assert(sizeof(cls) == size, "PXSIZE " #cls " " #size);
#define NESTED_CHECK_SIZEOF(parent, cls, size) static_assert(sizeof(parent::cls) == size, "PXSIZE " #parent "::" #cls " " #size);
#define CHECK_OFFSETOF(cls, member, offset) static_assert(offsetof(cls, member) == offset, "PXOFF " #cls "." #member " " #offset);
