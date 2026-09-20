#ifndef TCS_ADMIN_SCENARIO_H
#define TCS_ADMIN_SCENARIO_H
#include "tcs/admin.h"
static const struct tcs_admin_command scenario[] = {
    {1,0,{TCS_GRANT,1,TCS_OBJECT,TCS_READ,0}},
    {2,0,{TCS_GRANT,1,TCS_OBJECT,TCS_READ,0}},
    {3,1,{TCS_REVOKE,1,0,0,0}},
    {4,2,{TCS_GRANT,1,TCS_OBJECT,TCS_READ,0}},
    {5,3,{TCS_QUARANTINE,1,0,0,0}},
    {6,4,{TCS_GRANT,1,TCS_OBJECT,TCS_READ,0}},
    {7,4,{TCS_RESTORE,1,0,0,0}},
    {8,5,{TCS_GRANT,1,TCS_OBJECT,TCS_READ,0}},
    {9,6,{TCS_REVOKE,1,0,0,0}},
    {10,7,{TCS_GRANT,1,TCS_OBJECT,TCS_READ,0}},
    {11,7,{TCS_QUARANTINE,1,0,0,0}},
    {12,8,{TCS_RESTORE,1,0,0,0}},
};
#endif
