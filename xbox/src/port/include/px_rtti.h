/* px_rtti.h - exact-type checks for the few dynamic_casts the port's game code
 * makes. The Xbox build has no RTTI (-fno-rtti; nxdk has no __RTDynamicCast),
 * so a class with no subclasses is recognised by its vftable, which the MSVC
 * C++ ABI names ??_7<class>@@6B@ (emitted wherever the class is constructed). */
#pragma once

#define PX_VFTABLE(cls) extern "C" const void* const px_vftable_##cls __asm__("??_7" #cls "@@6B@");
#define PX_IS_EXACT(cls, p) ((p) != nullptr && *reinterpret_cast< const void* const* >(p) == &px_vftable_##cls)
#define PX_CAST_EXACT(cls, p) (PX_IS_EXACT(cls, p) ? static_cast< cls* >(p) : nullptr)
