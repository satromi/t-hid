/* PC 上の検証用: μT-Kernel 型定義の最小代替 */
#ifndef SHIM_TK_TYPEDEF_H
#define SHIM_TK_TYPEDEF_H
#include <stdint.h>
typedef int8_t   B;   typedef int16_t  H;   typedef int32_t  W;   typedef int64_t D;
typedef uint8_t  UB;  typedef uint16_t UH;  typedef uint32_t UW;  typedef uint64_t UD;
typedef int      INT; typedef unsigned int UINT;
typedef INT      ID;  typedef INT ER;  typedef INT BOOL;  typedef INT PRI;
typedef W        TMO; typedef UW RELTIM; typedef W SZ;
typedef void    (*FP)();
#define TRUE  1
#define FALSE 0
#define LOCAL  static
#define EXPORT
#define IMPORT extern
#define Inline static inline
#define CONST const
#define E_OK    0
#define E_SYS   (-5)
#define E_NOSPT (-9)
#define E_PAR   (-17)
#define E_NOMEM (-33)
#define E_LIMIT (-34)
#define E_OBJ   (-41)
#define E_NOEXS (-42)
#define E_IO    (-57)
#define E_BUSY  (-65)
#endif
