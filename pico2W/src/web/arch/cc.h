#ifndef __CC_H__
#define __CC_H__

typedef int sys_prot_t;

#if defined(__GNUC__)
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#else
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#endif

#define LWIP_PLATFORM_ASSERT(x) do { if (!(x)) while (1) {} } while (0)

#endif /* __CC_H__ */
