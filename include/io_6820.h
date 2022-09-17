/*******************************************
 * SPDX-License-Identifier: GPL-2.0-only   *
 * Copyright (C) 2017-2022  Martin Wolters *
 *******************************************/

#ifndef IO_6829_H_
#define IO_6829_H_

int pia_init(vm_t *vm);
void pia_step(vm_t *vm);
void pia_clean(void);

#endif