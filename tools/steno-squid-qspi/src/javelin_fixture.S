.section .rodata.javelin_fixture,"a",%progbits
.balign 4
.global javelin_fixture_start
.global javelin_fixture_end
javelin_fixture_start:
.incbin JAVELIN_FIXTURE_PATH
javelin_fixture_end:
