/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "dt_strided.h"

ucs_status_t ucp_dt_make_strided(unsigned count, size_t length, size_t stride,
                                 size_t prefix, ucp_datatype_t *datatype_p)
{
    ucp_datatype_t datatype;

    if ((length > UCS_MASK(UCP_DATATYPE_LENGTH_BITS)) ||
        (stride > UCS_MASK(UCP_DATATYPE_STRIDE_BITS)) ||
        (prefix > UCS_MASK(UCP_DATATYPE_PREFIX_BITS)) ||
        (count  > UCS_MASK(64 - UCP_DATATYPE_SHIFT
                              - UCP_DATATYPE_LENGTH_BITS
                              - UCP_DATATYPE_STRIDE_BITS
                              - UCP_DATATYPE_PREFIX_BITS))) {
        return UCS_ERR_EXCEEDS_LIMIT;
    }

    datatype    = count;
    datatype    = prefix               | (datatype << UCP_DATATYPE_PREFIX_BITS);
    datatype    = stride               | (datatype << UCP_DATATYPE_STRIDE_BITS);
    datatype    = length               | (datatype << UCP_DATATYPE_LENGTH_BITS);
    *datatype_p = UCP_DATATYPE_STRIDED | (datatype << UCP_DATATYPE_SHIFT);

    return UCS_OK;
}
