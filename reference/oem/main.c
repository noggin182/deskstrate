typedef unsigned char  undefined;

typedef unsigned char  undefined1;
typedef unsigned short undefined2;
typedef unsigned int   undefined4;


undefined LAB_080026dc;
undefined LAB_080026fc;
undefined4 DAT_e000ed08;
undefined4 DAT_08003000;
undefined4 DAT_200000dc;
undefined DAT_1fff0f04;
undefined4 DAT_40021040;
undefined4 DAT_20000254;
undefined4 DAT_40021034;
undefined4 DAT_20000258;
undefined4 DAT_2000025c;
undefined4 DAT_20000260;
undefined4 DAT_20000264;
undefined4 DAT_2000026c;
undefined4 DAT_20000268;
undefined4 DAT_20000270;
undefined4 DAT_20000274;
undefined DAT_40013800;
undefined DAT_080026a0;
undefined DAT_2000023c;
undefined DAT_200000c4;
undefined4 DAT_200002e0;
undefined * *PTR_DAT_08000000;
undefined4 DAT_40022014;
undefined4 DAT_40022010;
undefined DAT_50000000;
undefined DAT_50000400;
undefined UNK_50001400;
undefined2 DAT_200001f0;
undefined4 DAT_200001e4;
undefined2 DAT_200001f2;
undefined4 DAT_200001e8;
char DAT_200001f6;
undefined1 DAT_200001f5;
char DAT_200001f4;
undefined2 DAT_200001ec;
undefined4 DAT_200001dc;
undefined2 DAT_200001ee;
undefined4 DAT_200001e0;
int DAT_2000024c;
undefined4 DAT_40022100;
undefined4 DAT_40022120;
char DAT_200002dc;
undefined4 DAT_40021004;
undefined4 DAT_40022104;
undefined4 DAT_40022108;
undefined4 DAT_4002210c;
undefined LAB_080026bc;
undefined4 DAT_40022110;
undefined4 DAT_40022114;
undefined4 DAT_40022118;
undefined4 DAT_4002211c;
undefined4 DAT_40022008;
undefined4 DAT_40021800;
undefined4 DAT_40021880;
undefined4 DAT_40021804;
undefined4 DAT_40021884;
undefined4 DAT_40021860;
undefined4 DAT_20000244;
int DAT_20000244;
undefined4 DAT_200000c0;
uint DAT_20000248;
undefined4 DAT_e000e100;
undefined4 DAT_40021000;
undefined4 DAT_40021060;
undefined4 DAT_40022000;
undefined4 DAT_40021008;
uint DAT_200000c0;
undefined4 DAT_20000248;
undefined DAT_08002620;
undefined DAT_08002660;
undefined LAB_080026a8;
undefined4 DAT_40021010;
undefined4 DAT_e000e010;
undefined4 DAT_e000e014;
undefined4 DAT_e000e018;
undefined1 DAT_200000d0;
char DAT_200001f5;
char DAT_200000d1;
char DAT_200000d2;
undefined2 DAT_2000022c;
undefined4 DAT_20000228;
char DAT_20000236;
undefined2 DAT_20000234;
undefined4 DAT_20000230;
byte DAT_200001f5;
byte DAT_200001f6;
uint DAT_200000dc;
short DAT_20000238;
undefined2 DAT_20000224;
undefined4 DAT_20000220;
undefined1 DAT_200001f4;
undefined4 DAT_20000214;
undefined2 DAT_20000238;
undefined1 DAT_20000236;
undefined2 DAT_20000204;
undefined4 DAT_200001f8;
undefined2 DAT_20000206;
undefined4 DAT_200001fc;
undefined2 DAT_20000208;
undefined4 DAT_20000200;
undefined2 DAT_20000218;
undefined4 DAT_2000020c;
undefined2 DAT_2000021a;
undefined4 DAT_20000210;
undefined2 DAT_2000021c;
undefined DAT_e000e400;
undefined DAT_e000ed1c;
undefined1 DAT_200001f6;
undefined1 DAT_200001f7;
char DAT_200001f7;
undefined LAB_080025dc+1;
undefined DAT_20000250;

void thunk_FUN_080001e0(void)

{
  FUN_080001e0();
  return;
}



// WARNING: This function may have set the stack pointer

void Reset_Handler(void)

{
  FUN_08002050();
  FUN_08000164();
  FUN_080001e0();
  return;
}



int FUN_080000f0(uint param_1,uint param_2)

{
  int iVar1;
  uint uVar2;
  uint uVar3;
  
  iVar1 = 0;
  uVar2 = 0x20;
  while (uVar3 = uVar2 - 1, 0 < (int)uVar2) {
    uVar2 = uVar3;
    if (param_2 <= param_1 >> (uVar3 & 0xff)) {
      param_1 = param_1 - (param_2 << (uVar3 & 0xff));
      iVar1 = iVar1 + (1 << (uVar3 & 0xff));
    }
  }
  return iVar1;
}



void FUN_0800011c(undefined4 *param_1,undefined4 *param_2,uint param_3)

{
  undefined4 uVar1;
  bool bVar2;
  
  if ((((uint)param_1 | (uint)param_2) & 3) == 0) {
    for (; 3 < param_3; param_3 = param_3 - 4) {
      uVar1 = *param_2;
      param_2 = param_2 + 1;
      *param_1 = uVar1;
      param_1 = param_1 + 1;
    }
  }
  while (bVar2 = param_3 != 0, param_3 = param_3 - 1, bVar2) {
    *(undefined1 *)param_1 = *(undefined1 *)param_2;
    param_1 = (undefined4 *)((int)param_1 + 1);
    param_2 = (undefined4 *)((int)param_2 + 1);
  }
  return;
}



void FUN_08000140(undefined1 *param_1,int param_2,undefined1 param_3)

{
  bool bVar1;
  
  while (bVar1 = param_2 != 0, param_2 = param_2 + -1, bVar1) {
    *param_1 = param_3;
    param_1 = param_1 + 1;
  }
  return;
}



void FUN_0800014e(undefined4 param_1,undefined4 param_2)

{
  FUN_08000140(param_1,param_2,0);
  return;
}



undefined4 FUN_08000152(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  FUN_08000140(param_1,param_3,param_2);
  return param_1;
}



undefined4 FUN_08000164(void)

{
  byte bVar1;
  byte *pbVar2;
  byte *extraout_r1;
  byte *pbVar3;
  int extraout_r2;
  uint uVar4;
  int iVar5;
  uint uVar6;
  byte *pbVar7;
  undefined4 *puVar8;
  uint uVar9;
  
  for (puVar8 = (undefined4 *)&LAB_080026dc; puVar8 < &LAB_080026fc; puVar8 = puVar8 + 4) {
    (*(code *)puVar8[3])(*puVar8,puVar8[1],puVar8[2]);
  }
  thunk_FUN_080001e0();
  pbVar2 = (byte *)0x0;
  pbVar3 = extraout_r1;
  do {
    uVar9 = (uint)*pbVar2;
    uVar6 = uVar9 & 7;
    pbVar7 = pbVar2 + 1;
    if ((*pbVar2 & 7) == 0) {
      uVar6 = (uint)pbVar2[1];
      pbVar7 = pbVar2 + 2;
    }
    pbVar2 = pbVar7;
    uVar4 = (int)uVar9 >> 4;
    if (uVar4 == 0) {
      uVar4 = (uint)*pbVar2;
      pbVar2 = pbVar2 + 1;
    }
    while (uVar6 = uVar6 - 1, uVar6 != 0) {
      *pbVar3 = *pbVar2;
      pbVar2 = pbVar2 + 1;
      pbVar3 = pbVar3 + 1;
    }
    if ((int)(uVar9 << 0x1c) < 0) {
      bVar1 = *pbVar2;
      pbVar2 = pbVar2 + 1;
      pbVar7 = pbVar3 + -(uint)bVar1;
      iVar5 = uVar4 + 2;
      while (iVar5 = iVar5 + -1, -1 < iVar5) {
        *pbVar3 = *pbVar7;
        pbVar3 = pbVar3 + 1;
        pbVar7 = pbVar7 + 1;
      }
    }
    else {
      while (uVar4 = uVar4 - 1, -1 < (int)uVar4) {
        *pbVar3 = 0;
        pbVar3 = pbVar3 + 1;
      }
    }
  } while (pbVar3 < extraout_r1 + extraout_r2);
  return 0;
}



undefined4 FUN_08000188(byte *param_1,byte *param_2,int param_3)

{
  byte bVar1;
  uint uVar2;
  int iVar3;
  uint uVar4;
  byte *pbVar5;
  byte *pbVar6;
  uint uVar7;
  
  pbVar6 = param_2 + param_3;
  do {
    uVar7 = (uint)*param_1;
    uVar4 = uVar7 & 7;
    pbVar5 = param_1 + 1;
    if ((*param_1 & 7) == 0) {
      uVar4 = (uint)param_1[1];
      pbVar5 = param_1 + 2;
    }
    param_1 = pbVar5;
    uVar2 = (int)uVar7 >> 4;
    if (uVar2 == 0) {
      uVar2 = (uint)*param_1;
      param_1 = param_1 + 1;
    }
    while (uVar4 = uVar4 - 1, uVar4 != 0) {
      *param_2 = *param_1;
      param_1 = param_1 + 1;
      param_2 = param_2 + 1;
    }
    if ((int)(uVar7 << 0x1c) < 0) {
      bVar1 = *param_1;
      param_1 = param_1 + 1;
      pbVar5 = param_2 + -(uint)bVar1;
      iVar3 = uVar2 + 2;
      while (iVar3 = iVar3 + -1, -1 < iVar3) {
        *param_2 = *pbVar5;
        param_2 = param_2 + 1;
        pbVar5 = pbVar5 + 1;
      }
    }
    else {
      while (uVar2 = uVar2 - 1, -1 < (int)uVar2) {
        *param_2 = 0;
        param_2 = param_2 + 1;
      }
    }
  } while (param_2 < pbVar6);
  return 0;
}



undefined4 FUN_080001e0(void)

{
  uint uVar1;
  
  for (uVar1 = 0; uVar1 < 0x30; uVar1 = uVar1 + 1 & 0xff) {
    *(undefined4 *)(uVar1 * 4 + 0x20000000) = *(undefined4 *)(uVar1 * 4 + 0x8000000);
  }
  DAT_e000ed08 = 0x20000000;
  FUN_080025f8();
  return 0;
}



void FUN_08000214(void)

{
  uint uVar1;
  
  for (uVar1 = 0; uVar1 < 0x100; uVar1 = uVar1 + 4) {
  }
  return;
}



void FUN_0800022c(void)

{
  undefined4 local_1c;
  undefined4 *local_18;
  undefined4 local_14;
  undefined4 local_8;
  
  local_8 = 0;
  FUN_0800014e(&local_1c,0x14);
  local_1c = 2;
  local_18 = &DAT_08003000;
  local_14 = 2;
  FUN_08000884(&local_1c,&local_8);
  return;
}



void FUN_08000258(void)

{
  int iVar1;
  undefined4 *puVar2;
  undefined4 *puVar3;
  
  puVar2 = &DAT_08003000;
  puVar3 = &DAT_200000dc;
  while (puVar2 < (undefined4 *)0x8003100) {
    iVar1 = FUN_08000af0(1,puVar2,puVar3);
    if (iVar1 == 0) {
      puVar2 = puVar2 + 0x20;
      puVar3 = puVar3 + 0x20;
    }
  }
  return;
}



void FUN_08000288(void)

{
  uint uVar1;
  
  for (uVar1 = 0; uVar1 < 0x100; uVar1 = uVar1 + 4) {
  }
  return;
}



void FUN_080002a0(void)

{
  FUN_08001c78();
  FUN_08001bf8();
  FUN_08002034();
  return;
}



// WARNING: Globals starting with '_' overlap smaller symbols at the same address

void FUN_080002b0(void)

{
  undefined4 local_30;
  undefined4 local_2c;
  undefined4 local_28;
  undefined4 local_24;
  undefined4 local_20;
  undefined4 local_1c;
  undefined4 local_14;
  undefined4 local_10;
  uint local_c;
  undefined4 local_8;
  
  FUN_0800014e(&local_20,0x1c);
  local_30 = 0;
  local_2c = 0;
  local_28 = 0;
  local_24 = 0;
  local_20 = 0xb;
  local_14 = 0x100;
  local_10 = 0;
  local_c = _DAT_1fff0f04 & 0x1fff | 0x2000;
  local_1c = 0;
  local_8 = 0;
  FUN_080011e0(&local_20);
  local_30 = 7;
  local_2c = 0;
  local_28 = 0;
  local_24 = 0;
  FUN_08000f88(&local_30,0);
  return;
}



void FUN_0800030c(void)

{
  uint uVar1;
  undefined4 uVar2;
  int iVar3;
  
  uVar1 = DAT_40021040;
  DAT_40021040 = uVar1 | 0x4000;
  uVar2 = DAT_40021040;
  uVar1 = DAT_40021034;
  DAT_40021034 = uVar1 | 1;
  uVar2 = DAT_40021034;
  DAT_20000254 = &DAT_40013800;
  DAT_20000258 = 0x1c200;
  DAT_2000025c = 0;
  DAT_20000260 = 0;
  DAT_20000264 = 0;
  DAT_2000026c = 0;
  DAT_20000268 = 0xc;
  DAT_20000270 = 0;
  DAT_20000274 = 0;
  iVar3 = FUN_08001508(&DAT_20000254);
  if (iVar3 != 0) {
    FUN_08000420();
  }
  iVar3 = FUN_080016cc(&DAT_20000254);
  if (iVar3 != 0) {
    FUN_08000420();
  }
  return;
}



void FUN_080003a0(int param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  uint uVar1;
  uint local_1c [5];
  
  uVar1 = DAT_40021034;
  DAT_40021034 = uVar1 | 1;
  uVar1 = DAT_40021034;
  local_1c[0] = (uint)*(ushort *)(&DAT_080026a0 + param_1 * 2);
  local_1c[1] = 1;
  local_1c[2] = 0;
  local_1c[3] = 2;
  FUN_08000cdc(*(undefined4 *)(&DAT_2000023c + param_1 * 4),local_1c,&DAT_2000023c,param_4,uVar1 & 1
              );
  FUN_08000ea2(*(undefined4 *)(&DAT_2000023c + param_1 * 4),
               *(undefined2 *)(&DAT_080026a0 + param_1 * 2),1);
  return;
}



void FUN_08000408(void)

{
  FUN_080019f6(&DAT_20000254,&DAT_200000c4,1);
  return;
}



void FUN_08000420(void)

{
  return;
}



void FUN_08000424(void)

{
  uint uVar1;
  
  DAT_200002e0 = 0;
  uVar1 = DAT_40022014;
  DAT_40022014 = uVar1 | 4;
  PTR_DAT_08000000 = (undefined **)0x12344321;
  return;
}



void FUN_0800044c(undefined4 *param_1)

{
  uint uVar1;
  
  DAT_200002e0 = 0;
  uVar1 = DAT_40022014;
  DAT_40022014 = uVar1 | 2;
  *param_1 = 0xff;
  return;
}



void FUN_0800046c(undefined4 *param_1,undefined4 *param_2)

{
  bool bVar1;
  uint uVar2;
  byte bVar3;
  uint uVar4;
  
  bVar3 = 0;
  uVar4 = DAT_40022014;
  DAT_40022014 = uVar4 | 1;
  uVar4 = 0;
  bVar1 = (bool)isCurrentModePrivileged();
  if (bVar1) {
    uVar4 = isIRQinterruptsEnabled();
  }
  disableIRQinterrupts();
  while (bVar3 < 0x20) {
    *param_1 = *param_2;
    param_2 = param_2 + 1;
    param_1 = param_1 + 1;
    bVar3 = bVar3 + 1;
    if (bVar3 == 0x1f) {
      uVar2 = DAT_40022014;
      DAT_40022014 = uVar2 | 0x80000;
    }
  }
  bVar1 = (bool)isCurrentModePrivileged();
  if (bVar1) {
    enableIRQinterrupts((uVar4 & 1) == 1);
  }
  return;
}



void FUN_080004c0(undefined4 *param_1)

{
  uint uVar1;
  
  uVar1 = DAT_40022014;
  DAT_40022014 = uVar1 | 0x800;
  *param_1 = 0xff;
  return;
}



undefined4 FUN_080004d8(int param_1)

{
  int iVar1;
  uint uVar2;
  
  iVar1 = FUN_08000eb0();
  do {
    uVar2 = DAT_40022010;
    if ((uVar2 & 0x10000) == 0) {
      DAT_40022010 = 0x8011;
      return 0;
    }
    uVar2 = FUN_08000eb0();
  } while (uVar2 < (uint)(iVar1 + param_1));
  return 3;
}



void FUN_08000514(undefined *param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4,
                 undefined4 param_5)

{
  uint uVar1;
  undefined4 uVar2;
  undefined4 local_28;
  undefined4 local_24;
  undefined4 local_20;
  undefined4 local_1c;
  
  FUN_0800014e(&local_28,0x14);
  if (param_1 == &DAT_50000000) {
    uVar1 = DAT_40021034;
    DAT_40021034 = uVar1 | 1;
    uVar2 = DAT_40021034;
  }
  else if (param_1 == &DAT_50000400) {
    uVar1 = DAT_40021034;
    DAT_40021034 = uVar1 | 2;
    uVar2 = DAT_40021034;
  }
  else if (param_1 == &UNK_50001400) {
    uVar1 = DAT_40021034;
    DAT_40021034 = uVar1 | 0x20;
    uVar2 = DAT_40021034;
  }
  local_1c = param_5;
  local_28 = param_2;
  local_24 = param_3;
  local_20 = param_4;
  FUN_08000cdc(param_1,&local_28);
  return;
}



void FUN_080005b4(void)

{
  byte bVar1;
  int iVar2;
  
  if (DAT_200001f4 == '\0') {
    iVar2 = FUN_08000e90(DAT_200001dc,DAT_200001ec);
    if (iVar2 == 0) {
      bVar1 = 0;
      while (iVar2 = FUN_08000e90(DAT_200001dc,DAT_200001ec), iVar2 == 0) {
        bVar1 = bVar1 + 1;
        if (0x13 < bVar1) {
          if (DAT_200001f6 != '\0') {
            do {
              iVar2 = FUN_08000e90(DAT_200001dc,DAT_200001ec);
            } while (iVar2 == 0);
            DAT_200001f5 = 0;
            return;
          }
          bVar1 = 0;
          while( true ) {
            iVar2 = FUN_08000e90(DAT_200001dc,DAT_200001ec);
            if (iVar2 != 0) {
              return;
            }
            bVar1 = bVar1 + 1;
            if (99 < bVar1) break;
            FUN_08000858(10);
          }
          DAT_200001f4 = 1;
          DAT_200001f5 = 4;
          return;
        }
        FUN_08000858(1);
      }
    }
    else {
      iVar2 = FUN_08000e90(DAT_200001e0,DAT_200001ee);
      if (iVar2 == 0) {
        bVar1 = 0;
        while (iVar2 = FUN_08000e90(DAT_200001e0,DAT_200001ee), iVar2 == 0) {
          bVar1 = bVar1 + 1;
          if (0x13 < bVar1) {
            if (DAT_200001f6 != '\x01') {
              do {
                iVar2 = FUN_08000e90(DAT_200001e0,DAT_200001ee);
              } while (iVar2 == 0);
              DAT_200001f5 = 1;
              return;
            }
            bVar1 = 0;
            while( true ) {
              iVar2 = FUN_08000e90(DAT_200001e0,DAT_200001ee);
              if (iVar2 != 0) {
                return;
              }
              bVar1 = bVar1 + 1;
              if (99 < bVar1) break;
              FUN_08000858(10);
            }
            DAT_200001f4 = 1;
            DAT_200001f5 = 4;
            return;
          }
          FUN_08000858(1);
        }
      }
      else {
        iVar2 = FUN_08000e90(DAT_200001e4,DAT_200001f0);
        if (iVar2 == 0) {
          bVar1 = 0;
          while (iVar2 = FUN_08000e90(DAT_200001e4,DAT_200001f0), iVar2 == 0) {
            bVar1 = bVar1 + 1;
            if (0x13 < bVar1) {
              if (DAT_200001f6 != '\x02') {
                do {
                  iVar2 = FUN_08000e90(DAT_200001e4,DAT_200001f0);
                } while (iVar2 == 0);
                DAT_200001f5 = 2;
                return;
              }
              bVar1 = 0;
              while( true ) {
                iVar2 = FUN_08000e90(DAT_200001e4,DAT_200001f0);
                if (iVar2 != 0) {
                  return;
                }
                bVar1 = bVar1 + 1;
                if (99 < bVar1) break;
                FUN_08000858(10);
              }
              DAT_200001f4 = 1;
              DAT_200001f5 = 4;
              return;
            }
            FUN_08000858(1);
          }
        }
        else {
          iVar2 = FUN_08000e90(DAT_200001e8,DAT_200001f2);
          if (iVar2 == 0) {
            bVar1 = 0;
            while (iVar2 = FUN_08000e90(DAT_200001e8,DAT_200001f2), iVar2 == 0) {
              bVar1 = bVar1 + 1;
              if (0x13 < bVar1) {
                if (DAT_200001f6 != '\x03') {
                  do {
                    iVar2 = FUN_08000e90(DAT_200001e8,DAT_200001f2);
                  } while (iVar2 == 0);
                  DAT_200001f5 = 3;
                  return;
                }
                bVar1 = 0;
                while( true ) {
                  iVar2 = FUN_08000e90(DAT_200001e8,DAT_200001f2);
                  if (iVar2 != 0) {
                    return;
                  }
                  bVar1 = bVar1 + 1;
                  if (99 < bVar1) break;
                  FUN_08000858(10);
                }
                DAT_200001f4 = 1;
                DAT_200001f5 = 4;
                return;
              }
              FUN_08000858(1);
            }
          }
        }
      }
    }
  }
  else {
    iVar2 = FUN_08000e90(DAT_200001dc,DAT_200001ec);
    if ((((iVar2 == 1) && (iVar2 = FUN_08000e90(DAT_200001e0,DAT_200001ee), iVar2 == 1)) &&
        (iVar2 = FUN_08000e90(DAT_200001e4,DAT_200001f0), iVar2 == 1)) &&
       (iVar2 = FUN_08000e90(DAT_200001e8,DAT_200001f2), iVar2 == 1)) {
      DAT_200001f4 = '\0';
    }
  }
  return;
}



void FUN_08000858(uint param_1)

{
  int iVar1;
  int iVar2;
  
  iVar1 = FUN_08000eb0();
  if (param_1 != 0xffffffff) {
    param_1 = param_1 + DAT_2000024c;
  }
  do {
    iVar2 = FUN_08000eb0();
  } while ((uint)(iVar2 - iVar1) < param_1);
  return;
}



int FUN_08000884(int *param_1,uint *param_2)

{
  uint uVar1;
  int iVar2;
  uint uVar3;
  int iVar4;
  
  iVar4 = 1;
  if (DAT_200002dc == '\x01') {
    iVar4 = 2;
  }
  else {
    DAT_200002dc = 1;
    uVar3 = DAT_40021004;
    uVar1 = (uVar3 & 0xe000) >> 0xd;
    uVar3 = DAT_40022100;
    if ((((((uVar3 != **(byte **)(&LAB_080026bc + uVar1 * 4)) ||
           (uVar3 = DAT_40022104,
           uVar3 != (**(uint **)(&LAB_080026bc + uVar1 * 4) & 0x1ffffff) >> 0x10)) ||
          (uVar3 = DAT_40022110, uVar3 != **(ushort **)(&LAB_080026bc + uVar1 * 4) >> 8)) ||
         ((uVar3 = DAT_40022108, uVar3 != *(byte *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 4) ||
          (uVar3 = DAT_4002210c,
          uVar3 != (*(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 4) & 0x7ffffff) >> 0x10)))) ||
        ((uVar3 = DAT_40022114,
         uVar3 != (*(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 8) & 0x1ffff) ||
         ((uVar3 = DAT_40022118,
          uVar3 != (*(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 0xc) & 0x1ffff) ||
          (uVar3 = DAT_4002211c, uVar3 != *(ushort *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 0x10))))
         ))) || (uVar3 = DAT_40022120,
                uVar3 != (*(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 0x10) & 0x3fffffff) >>
                         0x10)) {
      DAT_40022100 = (uint)**(byte **)(&LAB_080026bc + uVar1 * 4);
      DAT_40022104 = (**(uint **)(&LAB_080026bc + uVar1 * 4) & 0x1ffffff) >> 0x10;
      DAT_40022110 = (uint)(**(ushort **)(&LAB_080026bc + uVar1 * 4) >> 8);
      DAT_40022108 = (uint)*(byte *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 4);
      DAT_4002210c = (*(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 4) & 0x7ffffff) >> 0x10;
      DAT_40022114 = *(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 8) & 0x1ffff;
      DAT_40022118 = *(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 0xc) & 0x1ffff;
      DAT_4002211c = (uint)*(ushort *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 0x10);
      DAT_40022120 = (*(uint *)(*(int *)(&LAB_080026bc + uVar1 * 4) + 0x10) & 0x3fffffff) >> 0x10;
    }
    if (*param_1 == 1) {
      iVar2 = FUN_080004d8(1000);
      if (iVar2 == 0) {
        FUN_08000424();
        iVar4 = FUN_080004d8(1000);
        uVar3 = DAT_40022014;
        DAT_40022014 = uVar3 & 0xfffffffb;
      }
    }
    else if (*param_1 == 2) {
      iVar2 = FUN_080004d8(1000);
      if (iVar2 == 0) {
        *param_2 = 0xffffffff;
        for (uVar3 = param_1[1]; uVar3 < (uint)(param_1[2] * 0x80 + param_1[1]);
            uVar3 = uVar3 + 0x80) {
          FUN_0800044c(uVar3);
          iVar4 = FUN_080004d8(1000);
          uVar1 = DAT_40022014;
          DAT_40022014 = uVar1 & 0xfffffffd;
          if (iVar4 != 0) {
            *param_2 = uVar3;
            break;
          }
        }
      }
    }
    else if ((*param_1 == 3) && (iVar2 = FUN_080004d8(1000), iVar2 == 0)) {
      *param_2 = 0xffffffff;
      for (uVar3 = param_1[3]; uVar3 < (uint)(param_1[4] * 0x1000 + param_1[3]);
          uVar3 = uVar3 + 0x1000) {
        FUN_080004c0(uVar3);
        iVar4 = FUN_080004d8(1000);
        uVar1 = DAT_40022014;
        DAT_40022014 = uVar1 & 0xfffff7ff;
        if (iVar4 != 0) {
          *param_2 = uVar3;
          break;
        }
      }
    }
    DAT_200002dc = '\0';
  }
  return iVar4;
}



bool FUN_08000acc(void)

{
  uint uVar1;
  
  uVar1 = DAT_40022014;
  DAT_40022014 = uVar1 | 0x80000000;
  uVar1 = DAT_40022014;
  return (uVar1 & 0x80000000) == 0;
}



int FUN_08000af0(undefined4 param_1,uint param_2,undefined4 param_3)

{
  uint uVar1;
  int iVar2;
  uint uVar3;
  int iVar4;
  
  if (DAT_200002dc == '\x01') {
    iVar2 = 2;
  }
  else {
    DAT_200002dc = '\x01';
    uVar1 = DAT_40021004;
    uVar3 = (uVar1 & 0xe000) >> 0xd;
    uVar1 = DAT_40022100;
    if ((((((uVar1 != **(byte **)(&LAB_080026bc + uVar3 * 4)) ||
           (uVar1 = DAT_40022104,
           uVar1 != (**(uint **)(&LAB_080026bc + uVar3 * 4) & 0x1ffffff) >> 0x10)) ||
          (uVar1 = DAT_40022110, uVar1 != **(ushort **)(&LAB_080026bc + uVar3 * 4) >> 8)) ||
         ((uVar1 = DAT_40022108, uVar1 != *(byte *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 4) ||
          (uVar1 = DAT_4002210c,
          uVar1 != (*(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 4) & 0x7ffffff) >> 0x10)))) ||
        ((uVar1 = DAT_40022114,
         uVar1 != (*(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 8) & 0x1ffff) ||
         ((uVar1 = DAT_40022118,
          uVar1 != (*(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 0xc) & 0x1ffff) ||
          (uVar1 = DAT_4002211c, uVar1 != *(ushort *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 0x10))))
         ))) || (uVar1 = DAT_40022120,
                uVar1 != (*(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 0x10) & 0x3fffffff) >>
                         0x10)) {
      DAT_40022100 = (uint)**(byte **)(&LAB_080026bc + uVar3 * 4);
      DAT_40022104 = (**(uint **)(&LAB_080026bc + uVar3 * 4) & 0x1ffffff) >> 0x10;
      DAT_40022110 = (uint)(**(ushort **)(&LAB_080026bc + uVar3 * 4) >> 8);
      DAT_40022108 = (uint)*(byte *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 4);
      DAT_4002210c = (*(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 4) & 0x7ffffff) >> 0x10;
      DAT_40022114 = *(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 8) & 0x1ffff;
      DAT_40022118 = *(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 0xc) & 0x1ffff;
      DAT_4002211c = (uint)*(ushort *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 0x10);
      DAT_40022120 = (*(uint *)(*(int *)(&LAB_080026bc + uVar3 * 4) + 0x10) & 0x3fffffff) >> 0x10;
    }
    if ((param_2 & 0x7f) == 0) {
      iVar2 = FUN_080004d8(1000);
      if (iVar2 == 0) {
        FUN_0800046c(param_2,param_3);
        iVar4 = FUN_080004d8(1000);
        uVar1 = DAT_40022014;
        DAT_40022014 = uVar1 & 0xfffffffe;
        iVar2 = 0;
        if (iVar4 != 0) {
          return iVar4;
        }
      }
      DAT_200002dc = '\0';
    }
    else {
      iVar2 = 1;
    }
  }
  return iVar2;
}



undefined4 FUN_08000ca8(void)

{
  uint uVar1;
  undefined4 uVar2;
  
  uVar2 = 0;
  uVar1 = DAT_40022014;
  if ((uVar1 & 0x80000000) != 0) {
    DAT_40022008 = 0x45670123;
    DAT_40022008 = 0xcdef89ab;
    uVar1 = DAT_40022014;
    if ((uVar1 & 0x80000000) != 0) {
      uVar2 = 1;
    }
  }
  return uVar2;
}



void FUN_08000cdc(uint *param_1,uint *param_2)

{
  uint uVar1;
  uint uVar2;
  int iVar3;
  uint uVar4;
  
  for (uVar1 = 0; *param_2 >> (uVar1 & 0xff) != 0; uVar1 = uVar1 + 1) {
    uVar2 = *param_2 & 1 << (uVar1 & 0xff);
    if (uVar2 != 0) {
      if ((param_2[1] == 2) || (param_2[1] == 0x12)) {
        param_1[(uVar1 >> 3) + 8] =
             param_1[(uVar1 >> 3) + 8] & ~(0xf << ((uVar1 & 7) << 2)) |
             param_2[4] << ((uVar1 & 7) << 2);
      }
      *param_1 = *param_1 & ~(3 << ((uVar1 & 0x7f) << 1)) |
                 ((byte)param_2[1] & 3) << ((uVar1 & 0x7f) << 1);
      if ((((param_2[1] == 1) || (param_2[1] == 2)) || (param_2[1] == 0x11)) || (param_2[1] == 0x12)
         ) {
        param_1[2] = param_1[2] & ~(3 << ((uVar1 & 0x7f) << 1)) |
                     param_2[3] << ((uVar1 & 0x7f) << 1);
        param_1[1] = param_1[1] & ~(1 << (uVar1 & 0xff)) |
                     ((param_2[1] & 0x10) >> 4) << (uVar1 & 0xff);
      }
      param_1[3] = param_1[3] & ~(3 << ((uVar1 & 0x7f) << 1)) | param_2[2] << ((uVar1 & 0x7f) << 1);
      if ((param_2[1] & 0x10000000) == 0x10000000) {
        if (param_1 == (uint *)&DAT_50000000) {
          iVar3 = 0;
        }
        else if (param_1 == (uint *)&DAT_50000400) {
          iVar3 = 1;
        }
        else {
          iVar3 = 2;
        }
        *(uint *)((int)&DAT_40021860 + (uVar1 & 0xfffffffc)) =
             *(uint *)((int)&DAT_40021860 + (uVar1 & 0xfffffffc)) & ~(0xf << ((uVar1 & 3) << 3)) |
             iVar3 << ((uVar1 & 3) << 3);
        uVar4 = DAT_40021880;
        uVar4 = uVar4 & ~uVar2;
        if ((param_2[1] & 0x10000) == 0x10000) {
          uVar4 = uVar4 | uVar2;
        }
        DAT_40021880 = uVar4;
        uVar4 = DAT_40021884;
        uVar4 = uVar4 & ~uVar2;
        if ((param_2[1] & 0x20000) == 0x20000) {
          uVar4 = uVar4 | uVar2;
        }
        DAT_40021884 = uVar4;
        uVar4 = DAT_40021800;
        uVar4 = uVar4 & ~uVar2;
        if ((param_2[1] & 0x100000) == 0x100000) {
          uVar4 = uVar4 | uVar2;
        }
        DAT_40021800 = uVar4;
        uVar4 = DAT_40021804;
        uVar4 = uVar4 & ~uVar2;
        if ((param_2[1] & 0x200000) == 0x200000) {
          uVar4 = uVar4 | uVar2;
        }
        DAT_40021804 = uVar4;
      }
    }
  }
  return;
}



bool FUN_08000e90(int param_1,uint param_2)

{
  return (*(uint *)(param_1 + 0x10) & param_2) != 0;
}



void FUN_08000ea2(int param_1,undefined4 param_2,int param_3)

{
  if (param_3 == 0) {
    *(undefined4 *)(param_1 + 0x28) = param_2;
  }
  else {
    *(undefined4 *)(param_1 + 0x18) = param_2;
  }
  return;
}



undefined4 FUN_08000eb0(void)

{
  return DAT_20000244;
}



void FUN_08000ebc(void)

{
  DAT_20000244 = DAT_20000244 + DAT_2000024c;
  return;
}



bool FUN_08000ed4(void)

{
  int iVar1;
  
  iVar1 = FUN_08000ef0(3);
  if (iVar1 == 0) {
    FUN_08000f50();
  }
  return iVar1 != 0;
}



undefined4 FUN_08000ef0(uint param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  undefined4 uVar1;
  int iVar2;
  undefined4 uVar3;
  
  uVar3 = 0;
  if (DAT_2000024c == 0) {
    uVar3 = 1;
  }
  else {
    uVar1 = FUN_080000f0(1000,DAT_2000024c,param_3,param_4,param_4);
    FUN_080000f0(DAT_200000c0,uVar1);
    iVar2 = FUN_080014d0();
    if (iVar2 == 0) {
      if (param_1 < 4) {
        FUN_08000f74(0xffffffff,param_1,0);
        DAT_20000248 = param_1;
      }
      else {
        uVar3 = 1;
      }
    }
    else {
      uVar3 = 1;
    }
  }
  return uVar3;
}



void FUN_08000f50(void)

{
  FUN_080003a0(0);
  return;
}



void FUN_08000f5c(uint param_1)

{
  DAT_e000e100 = 1 << (param_1 & 0x1f);
  return;
}



void FUN_08000f74(undefined4 param_1,undefined4 param_2)

{
  FUN_08001d94(param_1,param_2);
  return;
}



undefined4 FUN_08000f88(uint *param_1,uint param_2)

{
  uint uVar1;
  undefined4 uVar2;
  int iVar3;
  int iVar4;
  uint uVar5;
  
  if (param_1 == (uint *)0x0) {
    uVar2 = 1;
  }
  else {
    uVar1 = DAT_40022000;
    if ((uVar1 & 1) < param_2) {
      uVar1 = DAT_40022000;
      DAT_40022000 = uVar1 & 0xfffffffe | param_2;
      iVar3 = FUN_08000eb0();
      do {
        uVar1 = DAT_40022000;
        if ((uVar1 & 1) == param_2) goto LAB_08000fd4;
        iVar4 = FUN_08000eb0();
      } while ((uint)(iVar4 - iVar3) < 0x1389);
      uVar2 = 3;
    }
    else {
LAB_08000fd4:
      if ((*param_1 & 2) == 2) {
        if ((*param_1 & 4) == 4) {
          uVar1 = DAT_40021008;
          DAT_40021008 = (uVar1 & 0xffff8fff) + 0x7000;
        }
        uVar1 = DAT_40021008;
        DAT_40021008 = uVar1 & 0xfffff0ff | param_1[2];
      }
      if ((*param_1 & 1) == 0) {
LAB_0800108e:
        uVar1 = DAT_40022000;
        if (param_2 < (uVar1 & 1)) {
          uVar1 = DAT_40022000;
          DAT_40022000 = uVar1 & 0xfffffffe | param_2;
          iVar3 = FUN_08000eb0();
          do {
            uVar1 = DAT_40022000;
            if ((uVar1 & 1) == param_2) goto LAB_080010cc;
            iVar4 = FUN_08000eb0();
          } while ((uint)(iVar4 - iVar3) < 0x1389);
          uVar2 = 3;
        }
        else {
LAB_080010cc:
          if ((*param_1 & 4) == 4) {
            uVar1 = DAT_40021008;
            DAT_40021008 = uVar1 & 0xffff8fff | param_1[3];
          }
          uVar5 = FUN_08001160();
          uVar1 = DAT_40021008;
          DAT_200000c0 = uVar5 >> ((&DAT_08002620)[((uVar1 & 0xf00) >> 8) * 4] & 0x1f);
          uVar2 = FUN_08000ef0(DAT_20000248);
        }
      }
      else {
        if (param_1[1] == 1) {
          uVar1 = DAT_40021000;
          if ((uVar1 & 0x20000) == 0) {
            return 1;
          }
        }
        else if (param_1[1] == 0) {
          uVar1 = DAT_40021000;
          if ((uVar1 & 0x400) == 0) {
            return 1;
          }
        }
        else {
          uVar1 = DAT_40021060;
          if ((uVar1 & 2) == 0) {
            return 1;
          }
        }
        uVar1 = DAT_40021008;
        DAT_40021008 = uVar1 & 0xfffffff8 | param_1[1];
        iVar3 = FUN_08000eb0();
        do {
          uVar1 = DAT_40021008;
          if ((uVar1 & 0x38) == param_1[1] * 8) goto LAB_0800108e;
          iVar4 = FUN_08000eb0();
        } while ((uint)(iVar4 - iVar3) < 0x1389);
        uVar2 = 3;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_0800112c(void)

{
  return DAT_200000c0;
}



uint FUN_08001138(void)

{
  uint uVar1;
  uint uVar2;
  
  uVar2 = FUN_0800112c();
  uVar1 = DAT_40021008;
  return uVar2 >> ((&DAT_08002660)[((uVar1 & 0x7000) >> 0xc) * 4] & 0x1f);
}



undefined4 FUN_08001160(void)

{
  uint uVar1;
  undefined4 uVar2;
  uint uVar3;
  undefined4 local_24 [5];
  
  FUN_0800011c(local_24,&LAB_080026a8,0x14);
  uVar1 = DAT_40021008;
  if ((uVar1 & 0x38) == 0) {
    uVar1 = DAT_40021000;
    uVar3 = DAT_40021004;
    uVar3 = (uVar3 & 0xe000) >> 0xd;
    if (4 < uVar3) {
      uVar3 = 0;
    }
    uVar2 = FUN_080000f0(local_24[uVar3],1 << ((uVar1 & 0x3800) >> 0xb));
  }
  else {
    uVar1 = DAT_40021008;
    if ((uVar1 & 0x38) == 8) {
      uVar2 = 24000000;
    }
    else {
      uVar1 = DAT_40021008;
      if ((uVar1 & 0x38) == 0x18) {
        uVar2 = 0x8000;
      }
      else {
        uVar2 = 0;
      }
    }
  }
  return uVar2;
}



undefined4 FUN_080011e0(uint *param_1)

{
  uint uVar1;
  int iVar2;
  uint uVar3;
  int iVar4;
  
  if (param_1 == (uint *)0x0) {
    return 1;
  }
  if ((*param_1 & 1) != 0) {
    uVar1 = DAT_40021008;
    if ((uVar1 & 0x38) == 8) {
      uVar1 = DAT_40021000;
      if (((uVar1 & 0x20000) != 0) && (param_1[1] == 0)) {
        return 1;
      }
    }
    else {
      if (param_1[1] != 0) {
        if (param_1[2] == 0) {
          return 1;
        }
        uVar1 = DAT_40021010;
        DAT_40021010 = uVar1 & 0xfffffff3 | param_1[2];
      }
      if (param_1[1] == 0x10000) {
        uVar1 = DAT_40021000;
        DAT_40021000 = uVar1 | 0x10000;
      }
      else if (param_1[1] == 0x50000) {
        uVar1 = DAT_40021000;
        DAT_40021000 = uVar1 | 0x40000;
        uVar1 = DAT_40021000;
        DAT_40021000 = uVar1 | 0x10000;
      }
      else {
        uVar1 = DAT_40021000;
        DAT_40021000 = uVar1 & 0xfffeffff;
        uVar1 = DAT_40021000;
        DAT_40021000 = uVar1 & 0xfffbffff;
      }
      if (param_1[1] == 0) {
        iVar2 = FUN_08000eb0();
        while (uVar1 = DAT_40021000, (uVar1 & 0x20000) != 0) {
          iVar4 = FUN_08000eb0();
          if (200 < (uint)(iVar4 - iVar2)) {
            return 3;
          }
        }
      }
      else {
        iVar2 = FUN_08000eb0();
        while (uVar1 = DAT_40021000, (uVar1 & 0x20000) == 0) {
          iVar4 = FUN_08000eb0();
          if (200 < (uint)(iVar4 - iVar2)) {
            return 3;
          }
        }
      }
    }
  }
  if ((*param_1 & 2) == 2) {
    uVar1 = DAT_40021008;
    if ((uVar1 & 0x38) == 0) {
      uVar1 = DAT_40021000;
      if (((uVar1 & 0x400) != 0) && (param_1[3] == 0)) {
        return 1;
      }
      uVar1 = DAT_40021004;
      DAT_40021004 = uVar1 & 0xffff0000 | param_1[5];
      iVar2 = FUN_08000eb0();
      while (uVar1 = DAT_40021000, (uVar1 & 0x400) == 0) {
        iVar4 = FUN_08000eb0();
        if (2 < (uint)(iVar4 - iVar2)) {
          return 3;
        }
      }
      uVar1 = DAT_40021000;
      DAT_40021000 = uVar1 & 0xffffc7ff | param_1[4];
      uVar3 = FUN_08001160();
      uVar1 = DAT_40021008;
      DAT_200000c0 = uVar3 >> ((&DAT_08002620)[((uVar1 & 0xf00) >> 8) * 4] & 0x1f);
      iVar2 = FUN_08000ef0(DAT_20000248);
      if (iVar2 != 0) {
        return 1;
      }
    }
    else if (param_1[3] == 0) {
      uVar1 = DAT_40021000;
      DAT_40021000 = uVar1 & 0xfffffeff;
      iVar2 = FUN_08000eb0();
      while (uVar1 = DAT_40021000, (uVar1 & 0x400) != 0) {
        iVar4 = FUN_08000eb0();
        if (2 < (uint)(iVar4 - iVar2)) {
          return 3;
        }
      }
    }
    else {
      uVar1 = DAT_40021000;
      DAT_40021000 = uVar1 & 0xffffc7ff | param_1[4];
      uVar1 = DAT_40021000;
      DAT_40021000 = uVar1 | 0x100;
      iVar2 = FUN_08000eb0();
      while (uVar1 = DAT_40021000, (uVar1 & 0x400) == 0) {
        iVar4 = FUN_08000eb0();
        if (2 < (uint)(iVar4 - iVar2)) {
          return 3;
        }
      }
      uVar1 = DAT_40021004;
      DAT_40021004 = uVar1 & 0xffff0000 | param_1[5];
    }
  }
  if ((*param_1 & 8) == 8) {
    uVar1 = DAT_40021008;
    if ((uVar1 & 0x38) == 0x18) {
      uVar1 = DAT_40021060;
      if (((uVar1 & 2) != 0) && (param_1[6] == 0)) {
        return 1;
      }
    }
    else if (param_1[6] == 0) {
      uVar1 = DAT_40021060;
      DAT_40021060 = uVar1 & 0xfffffffe;
      iVar2 = FUN_08000eb0();
      while (uVar1 = DAT_40021060, (uVar1 & 2) != 0) {
        iVar4 = FUN_08000eb0();
        if (2 < (uint)(iVar4 - iVar2)) {
          return 3;
        }
      }
    }
    else {
      uVar1 = DAT_40021060;
      DAT_40021060 = uVar1 | 1;
      iVar2 = FUN_08000eb0();
      while (uVar1 = DAT_40021060, (uVar1 & 2) == 0) {
        iVar4 = FUN_08000eb0();
        if (2 < (uint)(iVar4 - iVar2)) {
          return 3;
        }
      }
    }
  }
  return 0;
}



bool FUN_080014d0(int param_1)

{
  bool bVar1;
  
  bVar1 = param_1 - 1U < 0x1000000;
  if (bVar1) {
    DAT_e000e014 = param_1 + -1;
    FUN_08001d94(0xffffffff);
    DAT_e000e018 = 0;
    DAT_e000e010 = 7;
  }
  return !bVar1;
}



bool FUN_08001508(int *param_1)

{
  if (param_1 != (int *)0x0) {
    *(undefined1 *)((int)param_1 + 0x3d) = 0x24;
    *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xffffdfff;
    FUN_08001756(param_1);
    param_1[0x10] = 0;
    *(undefined1 *)((int)param_1 + 0x3d) = 0;
    *(undefined1 *)((int)param_1 + 0x3e) = 0;
    *(undefined1 *)(param_1 + 0xf) = 0;
  }
  return param_1 == (int *)0x0;
}



void FUN_08001548(int param_1)

{
  FUN_08002430("Uart Error, ErrorCode = %d\r\n",*(undefined4 *)(param_1 + 0x40));
  return;
}



void FUN_08001578(int *param_1)

{
  uint uVar1;
  uint uVar2;
  uint uVar3;
  
  uVar1 = *(uint *)*param_1;
  uVar2 = *(uint *)(*param_1 + 0xc);
  uVar3 = *(uint *)(*param_1 + 0x14);
  if ((((uVar1 & 0xf) == 0) && ((uVar1 & 0x20) != 0)) && ((uVar2 & 0x20) != 0)) {
    FUN_080020fa(param_1);
  }
  else if (((uVar1 & 0xf) == 0) || (((uVar3 & 1) == 0 && ((uVar2 & 0x120) == 0)))) {
    if (((uVar1 & 0x10) != 0) && ((uVar2 & 0x10) != 0)) {
      FUN_080016ca(param_1);
    }
    if (((uVar1 & 0x80) == 0) || ((uVar2 & 0x80) == 0)) {
      if (((uVar1 & 0x40) != 0) && ((uVar2 & 0x40) != 0)) {
        FUN_080020da(param_1);
      }
    }
    else {
      FUN_08002328(param_1);
    }
  }
  else {
    if (((uVar1 & 1) != 0) && ((uVar2 & 0x100) != 0)) {
      param_1[0x10] = param_1[0x10] | 1;
    }
    if (((uVar1 & 4) != 0) && ((uVar3 & 1) != 0)) {
      param_1[0x10] = param_1[0x10] | 2;
    }
    if (((uVar1 & 2) != 0) && ((uVar3 & 1) != 0)) {
      param_1[0x10] = param_1[0x10] | 4;
    }
    if (((uVar1 & 8) != 0) && ((uVar3 & 1) != 0)) {
      param_1[0x10] = param_1[0x10] | 8;
    }
    if (param_1[0x10] != 0) {
      if (((uVar1 & 0x20) != 0) && ((uVar2 & 0x20) != 0)) {
        FUN_080020fa(param_1);
      }
      if ((param_1[0x10] & 8U) == 0) {
        FUN_08001548(param_1);
        param_1[0x10] = 0;
      }
      else {
        FUN_080020b8(param_1);
        FUN_08001548(param_1);
      }
    }
  }
  return;
}



void FUN_080016ca(void)

{
  return;
}



undefined4 FUN_080016cc(int *param_1)

{
  undefined4 uVar1;
  
  if (param_1 == (int *)0x0) {
    uVar1 = 1;
  }
  else {
    if (*(char *)((int)param_1 + 0x3d) == '\0') {
      *(undefined1 *)(param_1 + 0xf) = 0;
      FUN_08001758(param_1);
    }
    *(undefined1 *)((int)param_1 + 0x3d) = 0x24;
    *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xffffdfff;
    FUN_08002184(param_1);
    if (param_1[8] != 0) {
      FUN_08002080(param_1);
    }
    *(uint *)(*param_1 + 0x10) = *(uint *)(*param_1 + 0x10) & 0xfffff7ff;
    *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xfffffff7;
    *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) | 0x2000;
    param_1[0x10] = 0;
    *(undefined1 *)((int)param_1 + 0x3d) = 0x20;
    *(undefined1 *)((int)param_1 + 0x3e) = 0x20;
    uVar1 = 0;
  }
  return uVar1;
}



void FUN_08001756(void)

{
  return;
}



void FUN_08001758(void)

{
  undefined4 uVar1;
  uint uVar2;
  undefined4 extraout_r3;
  undefined4 local_1c;
  undefined4 local_18;
  undefined4 local_14;
  undefined4 local_10;
  undefined4 local_c;
  
  FUN_0800014e(&local_1c,0x14);
  uVar2 = DAT_40021034;
  DAT_40021034 = uVar2 | 1;
  uVar1 = DAT_40021034;
  uVar2 = DAT_40021040;
  DAT_40021040 = uVar2 | 0x4000;
  uVar2 = DAT_40021040;
  uVar2 = uVar2 & 0x4000;
  local_1c = 4;
  local_18 = 2;
  local_14 = 1;
  local_10 = 2;
  local_c = 1;
  FUN_08000cdc(&DAT_50000000,&local_1c);
  local_1c = 8;
  local_18 = 2;
  local_14 = 1;
  local_10 = 2;
  local_c = 1;
  FUN_08000cdc(&DAT_50000000,&local_1c);
  FUN_08000f74(0x1b,0,1,extraout_r3,uVar2);
  FUN_08000f5c(0x1b);
  return;
}



undefined4 FUN_080017fc(int *param_1,int param_2,int param_3)

{
  undefined4 uVar1;
  
  if (*(char *)((int)param_1 + 0x3e) == ' ') {
    if ((param_2 == 0) || (param_3 == 0)) {
      uVar1 = 1;
    }
    else if ((char)param_1[0xf] == '\x01') {
      uVar1 = 2;
    }
    else {
      *(undefined1 *)(param_1 + 0xf) = 1;
      param_1[0xd] = param_2;
      *(short *)(param_1 + 0xe) = (short)param_3;
      *(short *)((int)param_1 + 0x3a) = (short)param_3;
      param_1[0x10] = 0;
      *(undefined1 *)((int)param_1 + 0x3e) = 0x22;
      *(undefined1 *)(param_1 + 0xf) = 0;
      *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) | 0x100;
      *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) | 1;
      *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) | 0x20;
      uVar1 = 0;
    }
  }
  else {
    uVar1 = 2;
  }
  return uVar1;
}



void FUN_08001874(undefined4 *param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  ushort uVar1;
  int iVar2;
  uint uVar3;
  
  uVar1 = 0;
  uVar3 = *(uint *)*param_1;
  do {
    if ((uVar3 & 0x80) != 0x80) break;
    uVar1 = uVar1 + 1;
    uVar3 = *(uint *)*param_1;
  } while (uVar1 < 0x3e9);
  iVar2 = FUN_080019f6(param_1,&DAT_200000d0,3,param_4,param_4);
  if (iVar2 != 0) {
    FUN_08000420();
  }
  if (DAT_200000d0 == -0x56) {
    if ((char)(DAT_200000d1 + -0x56 + DAT_200000d2) == '\0') {
      DAT_200001f5 = DAT_200000d1;
    }
  }
  else {
    DAT_200001f5 = -1;
    FUN_08000420();
  }
  return;
}



undefined4 FUN_080018f8(int *param_1,ushort *param_2,int param_3,undefined4 param_4)

{
  undefined4 uVar1;
  int iVar2;
  int iVar3;
  ushort *puVar4;
  ushort *puVar5;
  int *piVar6;
  ushort *puVar7;
  
  if (*(char *)((int)param_1 + 0x3d) == ' ') {
    if ((param_2 == (ushort *)0x0) || (param_3 == 0)) {
      uVar1 = 1;
    }
    else if (((param_1[2] == 0x1000) && (param_1[4] == 0)) && (((uint)param_2 & 1) != 0)) {
      uVar1 = 1;
    }
    else if ((char)param_1[0xf] == '\x01') {
      uVar1 = 2;
    }
    else {
      *(undefined1 *)(param_1 + 0xf) = 1;
      param_1[0x10] = 0;
      *(undefined1 *)((int)param_1 + 0x3d) = 0x21;
      piVar6 = param_1;
      puVar7 = param_2;
      iVar3 = param_3;
      uVar1 = FUN_08000eb0();
      *(short *)(param_1 + 0xc) = (short)param_3;
      *(short *)((int)param_1 + 0x32) = (short)param_3;
      if ((param_1[2] == 0x1000) && (param_1[4] == 0)) {
        puVar4 = (ushort *)0x0;
        puVar5 = param_2;
      }
      else {
        puVar5 = (ushort *)0x0;
        puVar4 = param_2;
      }
      while (*(short *)((int)param_1 + 0x32) != 0) {
        iVar2 = FUN_08002392(param_1,0x80,0,uVar1,param_4,uVar1,puVar5,piVar6,puVar7,iVar3);
        if (iVar2 != 0) {
          return 3;
        }
        if (puVar4 == (ushort *)0x0) {
          *(uint *)(*param_1 + 4) = *puVar5 & 0x1ff;
          puVar5 = puVar5 + 1;
        }
        else {
          *(uint *)(*param_1 + 4) = (uint)(byte)*puVar4;
          puVar4 = (ushort *)((int)puVar4 + 1);
        }
        *(short *)((int)param_1 + 0x32) = *(short *)((int)param_1 + 0x32) + -1;
      }
      iVar3 = FUN_08002392(param_1,0x40,0,uVar1,param_4,uVar1,puVar5,piVar6,puVar7,iVar3);
      if (iVar3 == 0) {
        *(undefined1 *)((int)param_1 + 0x3d) = 0x20;
        *(undefined1 *)(param_1 + 0xf) = 0;
        uVar1 = 0;
      }
      else {
        uVar1 = 3;
      }
    }
  }
  else {
    uVar1 = 2;
  }
  return uVar1;
}



undefined4 FUN_080019f6(int *param_1,int param_2,int param_3)

{
  undefined4 uVar1;
  
  if (*(char *)((int)param_1 + 0x3d) == ' ') {
    if ((param_2 == 0) || (param_3 == 0)) {
      uVar1 = 1;
    }
    else if ((char)param_1[0xf] == '\x01') {
      uVar1 = 2;
    }
    else {
      *(undefined1 *)(param_1 + 0xf) = 1;
      param_1[0xb] = param_2;
      *(short *)(param_1 + 0xc) = (short)param_3;
      *(short *)((int)param_1 + 0x32) = (short)param_3;
      param_1[0x10] = 0;
      *(undefined1 *)((int)param_1 + 0x3d) = 0x21;
      *(undefined1 *)(param_1 + 0xf) = 0;
      *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) | 0x80;
      uVar1 = 0;
    }
  }
  else {
    uVar1 = 2;
  }
  return uVar1;
}



void FUN_08001a54(undefined4 param_1)

{
  int iVar1;
  
  iVar1 = FUN_080017fc(param_1,&DAT_200000d0,3);
  if (iVar1 != 0) {
    FUN_08000420();
  }
  return;
}



void FUN_08001a74(void)

{
  uint uVar1;
  
  if (DAT_20000238 != 0) {
    DAT_20000238 = DAT_20000238 + -1;
    if (DAT_20000238 == 0) {
      FUN_08000ea2(DAT_20000220,DAT_20000224,1);
      FUN_08000ea2(DAT_20000228,DAT_2000022c,0);
      if (DAT_20000236 == '\0') {
        FUN_08000ea2(DAT_20000230,DAT_20000234,1);
      }
      else {
        FUN_08000ea2(DAT_20000230,DAT_20000234,0);
      }
    }
  }
  if (DAT_200001f5 != 0xff) {
    FUN_08002450();
    DAT_200001f6 = DAT_200001f5;
    FUN_08000ea2(DAT_20000220,DAT_20000224,0);
    FUN_08000ea2(DAT_20000228,DAT_2000022c,1);
    DAT_20000238 = 0x4fff;
    if (DAT_20000236 == '\0') {
      uVar1 = 0;
    }
    else {
      uVar1 = 0x100;
    }
    DAT_200000dc = uVar1 | 0xffff0000 | (uint)DAT_200001f6;
    FUN_08002414();
    DAT_200001f5 = 0xff;
  }
  return;
}



void FUN_08001bf8(void)

{
  FUN_08000514(DAT_200001dc,DAT_200001ec,0,1,2);
  FUN_08000514(DAT_200001e0,DAT_200001ee,0,1,2);
  FUN_08000514(DAT_200001e4,DAT_200001f0,0,1,2);
  FUN_08000514(DAT_200001e8,DAT_200001f2,0,1,2);
  DAT_200001f4 = 0;
  return;
}



void FUN_08001c78(void)

{
  FUN_08000514(DAT_200001f8,DAT_20000204,1,0,2);
  FUN_08000514(DAT_200001fc,DAT_20000206,1,0,2);
  FUN_08000514(DAT_20000200,DAT_20000208,1,0,2);
  FUN_08000514(DAT_2000020c,DAT_20000218,1,0,2);
  FUN_08000514(DAT_20000210,DAT_2000021a,1,0,2);
  FUN_08000514(DAT_20000214,DAT_2000021c,1,0,2);
  FUN_08000514(DAT_20000220,DAT_20000224,1,0,2);
  FUN_08000514(DAT_20000228,DAT_2000022c,1,0,2);
  FUN_08000514(DAT_20000230,DAT_20000234,1,0,2);
  DAT_20000238 = 0xfff;
  DAT_20000236 = 1;
  return;
}



void FUN_08001d94(uint param_1,uint param_2)

{
  if ((int)param_1 < 0) {
    *(uint *)(&DAT_e000ed1c + ((param_1 & 0xf) - 8 & 0xfffffffc)) =
         *(uint *)(&DAT_e000ed1c + ((param_1 & 0xf) - 8 & 0xfffffffc)) &
         ~(0xff << ((param_1 & 3) << 3)) | ((param_2 & 3) << 6) << ((param_1 & 3) << 3);
  }
  else {
    *(uint *)(&DAT_e000e400 + (param_1 & 0xfffffffc)) =
         *(uint *)(&DAT_e000e400 + (param_1 & 0xfffffffc)) & ~(0xff << ((param_1 & 3) << 3)) |
         ((param_2 & 3) << 6) << ((param_1 & 3) << 3);
  }
  return;
}



// WARNING: Removing unreachable block (ram,0x08001e28)
// WARNING: Removing unreachable block (ram,0x08001e3e)

void FUN_08001e10(void)

{
  DAT_200000dc = 0xffff0002;
  DAT_200001f6 = 2;
  DAT_20000236 = 0;
  DAT_200001f5 = 0xff;
  DAT_200001f7 = 0xff;
  return;
}



void FUN_08001e70(void)

{
  if (DAT_200001f6 != DAT_200001f7) {
    DAT_200001f7 = DAT_200001f6;
    if (DAT_200001f6 == '\0') {
      FUN_08000ea2(DAT_200001f8,DAT_20000204,0);
      FUN_08000ea2(DAT_200001fc,DAT_20000206,0);
      FUN_08000ea2(DAT_20000200,DAT_20000208,1);
      FUN_08000ea2(DAT_2000020c,DAT_20000218,0);
      FUN_08000ea2(DAT_20000210,DAT_2000021a,0);
      FUN_08000ea2(DAT_20000214,DAT_2000021c,0);
    }
    else if (DAT_200001f6 == '\x01') {
      FUN_08000ea2(DAT_200001f8,DAT_20000204,1);
      FUN_08000ea2(DAT_200001fc,DAT_20000206,1);
      FUN_08000ea2(DAT_20000200,DAT_20000208,0);
      FUN_08000ea2(DAT_2000020c,DAT_20000218,0);
      FUN_08000ea2(DAT_20000210,DAT_2000021a,1);
      FUN_08000ea2(DAT_20000214,DAT_2000021c,1);
    }
    else if (DAT_200001f6 == '\x02') {
      FUN_08000ea2(DAT_200001f8,DAT_20000204,1);
      FUN_08000ea2(DAT_200001fc,DAT_20000206,0);
      FUN_08000ea2(DAT_20000200,DAT_20000208,1);
      FUN_08000ea2(DAT_2000020c,DAT_20000218,1);
      FUN_08000ea2(DAT_20000210,DAT_2000021a,0);
      FUN_08000ea2(DAT_20000214,DAT_2000021c,0);
    }
    else if (DAT_200001f6 == '\x03') {
      FUN_08000ea2(DAT_200001f8,DAT_20000204,0);
      FUN_08000ea2(DAT_200001fc,DAT_20000206,1);
      FUN_08000ea2(DAT_20000200,DAT_20000208,0);
      FUN_08000ea2(DAT_2000020c,DAT_20000218,1);
      FUN_08000ea2(DAT_20000210,DAT_2000021a,1);
      FUN_08000ea2(DAT_20000214,DAT_2000021c,1);
    }
  }
  return;
}



void FUN_08002034(void)

{
  FUN_08000ea2(&DAT_50000400,2,1);
  return;
}



// WARNING: Globals starting with '_' overlap smaller symbols at the same address

void FUN_08002050(void)

{
  uint uVar1;
  
  uVar1 = DAT_40021004;
  DAT_40021004 = (uVar1 & 0xffff0000) + 0x2000 | _DAT_1fff0f04 & 0x1fff;
  DAT_e000ed08 = 0x8000000;
  return;
}



void FUN_08002080(int *param_1)

{
  if (((param_1[8] & 0x40U) != 0) &&
     (*(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xffffefff | param_1[9],
     param_1[9] == 0x1000)) {
    *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xffff9fff | param_1[10];
  }
  return;
}



void FUN_080020b8(int *param_1)

{
  *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xfffffedf;
  *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xfffffffe;
  *(undefined1 *)((int)param_1 + 0x3e) = 0x20;
  return;
}



undefined4 FUN_080020da(int *param_1)

{
  *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xffffffbf;
  *(undefined1 *)((int)param_1 + 0x3d) = 0x20;
  FUN_08001a54(param_1);
  return 0;
}



undefined4 FUN_080020fa(int *param_1)

{
  short sVar1;
  undefined4 uVar2;
  undefined1 *puVar3;
  
  if (*(char *)((int)param_1 + 0x3e) == '\"') {
    if ((param_1[2] == 0x1000) && (param_1[4] == 0)) {
      *(ushort *)param_1[0xd] = (ushort)*(undefined4 *)(*param_1 + 4) & 0x1ff;
      param_1[0xd] = param_1[0xd] + 2;
    }
    else {
      uVar2 = *(undefined4 *)(*param_1 + 4);
      puVar3 = (undefined1 *)param_1[0xd];
      param_1[0xd] = (int)(puVar3 + 1);
      *puVar3 = (char)uVar2;
    }
    sVar1 = *(short *)((int)param_1 + 0x3a) + -1;
    *(short *)((int)param_1 + 0x3a) = sVar1;
    if (sVar1 == 0) {
      *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xffffffdf;
      *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xfffffeff;
      *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xfffffffe;
      *(undefined1 *)((int)param_1 + 0x3e) = 0x20;
      FUN_08001874(param_1);
      uVar2 = 0;
    }
    else {
      uVar2 = 0;
    }
  }
  else {
    uVar2 = 2;
  }
  return uVar2;
}



void FUN_08002184(int *param_1)

{
  int iVar1;
  undefined4 uVar2;
  int iVar3;
  uint uVar4;
  uint uVar5;
  
  *(uint *)(*param_1 + 0x10) = *(uint *)(*param_1 + 0x10) & 0xffffdfff | param_1[3];
  *(uint *)(*param_1 + 0xc) =
       *(uint *)(*param_1 + 0xc) & 0xffffe9f3 | param_1[2] | param_1[4] | param_1[5];
  *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xfffff7ff | param_1[7];
  *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xfffffcff | param_1[6];
  if (param_1[7] == 0x800) {
    iVar1 = FUN_08001138();
    uVar2 = FUN_080000f0(iVar1 * 0x19,param_1[1] << 1);
    iVar3 = FUN_080000f0(uVar2,100);
    FUN_080000f0(iVar1 * 0x19,param_1[1] << 1);
    uVar2 = FUN_080000f0(iVar1 * 0x19,param_1[1] << 1);
    FUN_080000f0(uVar2,100);
    uVar4 = FUN_080000f0();
    FUN_080000f0(iVar1 * 0x19,param_1[1] << 1);
    uVar2 = FUN_080000f0(iVar1 * 0x19,param_1[1] << 1);
    FUN_080000f0(uVar2,100);
    uVar5 = FUN_080000f0();
    *(uint *)(*param_1 + 8) = iVar3 * 0x10 + (uVar4 & 0xf8) * 2 + (uVar5 & 7);
  }
  else {
    iVar1 = FUN_08001138();
    uVar2 = FUN_080000f0(iVar1 * 0x19,param_1[1] << 2);
    iVar3 = FUN_080000f0(uVar2,100);
    FUN_080000f0(iVar1 * 0x19,param_1[1] << 2);
    uVar2 = FUN_080000f0(iVar1 * 0x19,param_1[1] << 2);
    FUN_080000f0(uVar2,100);
    uVar4 = FUN_080000f0();
    FUN_080000f0(iVar1 * 0x19,param_1[1] << 2);
    uVar2 = FUN_080000f0(iVar1 * 0x19,param_1[1] << 2);
    FUN_080000f0(uVar2,100);
    uVar5 = FUN_080000f0();
    *(uint *)(*param_1 + 8) = iVar3 * 0x10 + (uVar4 & 0xf0) + (uVar5 & 0xf);
  }
  return;
}



undefined4 FUN_08002328(int *param_1)

{
  short sVar1;
  undefined4 uVar2;
  
  if (*(char *)((int)param_1 + 0x3d) == '!') {
    if ((param_1[2] == 0x1000) && (param_1[4] == 0)) {
      *(uint *)(*param_1 + 4) = *(ushort *)param_1[0xb] & 0x1ff;
      param_1[0xb] = param_1[0xb] + 2;
    }
    else {
      *(uint *)(*param_1 + 4) = (uint)*(byte *)param_1[0xb];
      param_1[0xb] = param_1[0xb] + 1;
    }
    sVar1 = *(short *)((int)param_1 + 0x32) + -1;
    *(short *)((int)param_1 + 0x32) = sVar1;
    if (sVar1 == 0) {
      *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xffffff7f;
      *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) | 0x40;
    }
    uVar2 = 0;
  }
  else {
    uVar2 = 2;
  }
  return uVar2;
}



undefined4 FUN_08002392(int *param_1,uint param_2,uint param_3,int param_4,uint param_5)

{
  int iVar1;
  
  do {
    if (((*(uint *)*param_1 & param_2) == param_2) != param_3) {
      return 0;
    }
  } while ((param_5 == 0xffffffff) ||
          ((param_5 != 0 && (iVar1 = FUN_08000eb0(), (uint)(iVar1 - param_4) <= param_5))));
  *(uint *)(*param_1 + 0xc) = *(uint *)(*param_1 + 0xc) & 0xfffffe5f;
  *(uint *)(*param_1 + 0x14) = *(uint *)(*param_1 + 0x14) & 0xfffffffe;
  *(undefined1 *)((int)param_1 + 0x3d) = 0x20;
  *(undefined1 *)((int)param_1 + 0x3e) = 0x20;
  *(undefined1 *)(param_1 + 0xf) = 0;
  return 3;
}



void FUN_08002414(void)

{
  FUN_08000ca8();
  FUN_0800022c();
  FUN_08000214();
  FUN_08000258();
  FUN_08000acc();
  FUN_08000288();
  return;
}



void FUN_08002430(undefined4 param_1,undefined4 param_2,undefined4 param_3,undefined4 param_4)

{
  undefined4 uStack_c;
  undefined4 uStack_8;
  undefined4 uStack_4;
  
  uStack_c = param_2;
  uStack_8 = param_3;
  uStack_4 = param_4;
  FUN_0800248c(param_1,&uStack_c,&DAT_20000250,&LAB_080025dc_1);
  return;
}



void FUN_08002450(void)

{
  uint in_r3;
  uint uVar1;
  int unaff_lr;
  
  uVar1 = (uint)*(byte *)(unaff_lr + -1);
  if (in_r3 < *(byte *)(unaff_lr + -1)) {
    uVar1 = in_r3;
  }
                    // WARNING: Could not recover jumptable at 0x08002466. Too many branches
                    // WARNING: Treating indirect jump as call
  (*(code *)(unaff_lr + (uint)*(byte *)(unaff_lr + uVar1) * 2))();
  return;
}



int FUN_0800248c(byte *param_1,int *param_2,undefined4 param_3,code *param_4)

{
  byte bVar1;
  uint uVar2;
  int iVar3;
  char extraout_r1;
  byte *pbVar4;
  int iVar5;
  int *piVar6;
  uint uVar7;
  int local_58;
  int local_54;
  undefined1 local_50 [35];
  undefined1 local_2d;
  int local_2c [2];
  byte *pbStack_24;
  int *local_20;
  undefined4 local_1c;
  code *local_18;
  
  local_18 = param_4;
  local_1c = param_3;
  pbStack_24 = param_1;
  iVar5 = 0;
  local_20 = param_2;
  do {
    while( true ) {
      bVar1 = *param_1;
      if (bVar1 == 0) {
        return iVar5;
      }
      if (bVar1 == 0x25) break;
      (*local_18)(bVar1,local_1c);
      param_1 = param_1 + 1;
      iVar5 = iVar5 + 1;
    }
    uVar7 = 0;
    local_58 = 0;
    while( true ) {
      pbVar4 = param_1 + 1;
      uVar2 = 1 << (*pbVar4 - 0x20 & 0xff);
      if ((uVar2 & 0x12809) == 0) break;
      uVar7 = uVar7 | uVar2;
      param_1 = pbVar4;
    }
    if (*pbVar4 == 0x2e) {
      uVar7 = uVar7 | 4;
      pbVar4 = param_1 + 2;
      if (param_1[2] == 0x2a) {
        pbVar4 = param_1 + 3;
        local_58 = *local_20;
        local_20 = local_20 + 1;
      }
      else {
        for (; *pbVar4 - 0x30 < 10; pbVar4 = pbVar4 + 1) {
          local_58 = (uint)*pbVar4 + local_58 * 10 + -0x30;
        }
      }
    }
    bVar1 = *pbVar4;
    if (bVar1 == 0) {
      return iVar5;
    }
    if ((bVar1 == 100) || (bVar1 == 0x69)) {
      iVar3 = *local_20;
      local_20 = local_20 + 1;
      if (iVar3 < 0) {
        iVar3 = -iVar3;
        local_50[0] = 0x2d;
      }
      else if ((int)(uVar7 << 0x14) < 0) {
        local_50[0] = 0x2b;
      }
      else {
        if ((uVar7 & 1) == 0) goto LAB_0800254c;
        local_50[0] = 0x20;
      }
      local_2c[0] = 1;
LAB_08002546:
      piVar6 = local_2c;
      while (iVar3 != 0) {
        iVar3 = FUN_080000f0(iVar3,10);
        piVar6 = (int *)((int)piVar6 + -1);
        *(char *)piVar6 = extraout_r1 + '0';
      }
      local_54 = (int)local_2c - (int)piVar6;
      if (-1 < (int)(uVar7 << 0x1d)) {
        local_58 = 1;
      }
      if (local_54 < local_58) {
        local_58 = local_58 - local_54;
      }
      else {
        local_58 = 0;
      }
      for (iVar3 = 0; iVar3 < local_2c[0]; iVar3 = iVar3 + 1) {
        (*local_18)(local_50[iVar3],local_1c);
        iVar5 = iVar5 + 1;
      }
      while (0 < local_58) {
        (*local_18)(0x30,local_1c);
        iVar5 = iVar5 + 1;
        local_58 = local_58 + -1;
      }
      while (0 < local_54) {
        iVar3 = *piVar6;
        piVar6 = (int *)((int)piVar6 + 1);
        (*local_18)((char)iVar3,local_1c);
        iVar5 = iVar5 + 1;
        local_54 = local_54 + -1;
      }
    }
    else {
      if (bVar1 == 0x75) {
        iVar3 = *local_20;
        local_20 = local_20 + 1;
LAB_0800254c:
        local_2c[0] = 0;
        goto LAB_08002546;
      }
      (*local_18)(bVar1,local_1c);
      iVar5 = iVar5 + 1;
    }
    param_1 = pbVar4 + 1;
  } while( true );
}



void FUN_080025f8(void)

{
  FUN_08000ed4();
  FUN_080002b0();
  FUN_0800030c();
  FUN_080002a0();
  FUN_08001e10();
  FUN_08000408();
  do {
    FUN_080005b4();
    FUN_08001e70();
    FUN_08001a74();
  } while( true );
}


