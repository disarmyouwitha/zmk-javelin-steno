.section .rodata.steno_dict,"a",%progbits
.global _javelin_dict_start
.global _javelin_dict_end
.balign 4
_javelin_dict_start:
.byte 74,83,67,52,1,0,0,0,14,0,0,0
.long _javelin_dict_start + 124
.byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.byte 120,86,52,18
.long _javelin_dict_start + 40
.byte 1,1,0,0
.long _javelin_dict_start + 56
.long _javelin_dict_start + 124
.long _javelin_dict_start + 56
.byte 99,97,116,45,116,101,115,116,0,0,0,0,127,0,0,0
.long _javelin_dict_start + 80
.long _javelin_dict_start + 104
.byte 10,0,0,0,0,8,7,0,0,2,0,0,0,0,0,8
.byte 1,8,4,0,0,20,0,0,0,0,144,0,0,0,0,0
.byte 0,16,16,0,0,0,0,0,255,255,255,255,99,97,116,0
.byte 105,102,0,105,115,0,116,104,101,0,120,86,52,18
_javelin_dict_end:
