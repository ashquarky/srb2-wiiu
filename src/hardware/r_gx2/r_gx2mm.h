//
// Created by ash on 10/09/24.
//

#ifndef SRB2_R_GX2MM_H
#define SRB2_R_GX2MM_H

#include <stdlib.h>

void gx2mm_init(void);
void gx2mm_reset(void);
void *gx2mm_alloc(size_t size, size_t align);

#endif //SRB2_R_GX2MM_H
