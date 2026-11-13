#ifndef ZNMEASSET_H
#define ZNMEASSET_H

#include <types.h>

struct zScene;

void zNMEAsset_RegisterEnemies(zScene* zscn, S32* base_idx);
void zNMEAsset_CreateEnemies();
S32 zNMEAsset_CountEnemies(S32* num_nme, S32* num_INnpc);

#endif
