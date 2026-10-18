/* Observed ELF RVAs for 150-105c only. */
tab = 0x44a40;
header = 0x32700;
text = 0x1fe1b0;
label = 0x3a2b0;
field = 0x3a6f0;
same_line = 0x1cf490;
begin_group = 0x1cf2d0;
end_group = 0x1cf710;
pop_id = 0x1cf150;
pop_vars = 0x1cd400;
pop_colors = 0x1cd040;
unindent = 0x1d0870;
cursor_x = 0x1ce9a0;
cursor_y = 0x1ce9d0;
cursor_set = 0x1cea00;
combo = 0x210260;
input = 0x20a520;
button = 0x82290;
vendor_getenv = 0x9740;
vendor_roundf = 0x9610;
ui_context = 0x359868;
vendor_card = 0x53e20;
vendor_host_list = 0x4f120;
vendor_action_button = 0x382d0;
vendor_config_string = 0x83ed0;
vendor_icon_button = 0x340b0;
vendor_clipboard_set = 0x1c9ac0;
vendor_guest_strip = 0x527c0;
vendor_guest_provider = 0x69ae0;
vendor_host_command = 0x69830;
vendor_calloc = 0x9390;
SECTIONS {
  . = 0x401000;
  .extension : { *(.text*) *(.rodata*) }
  . = ALIGN(0x1000);
  .state : { *(.bss*) *(COMMON) }
  .unexpected : { *(.data*) *(.got*) }
  ASSERT(SIZEOF(.unexpected) == 0, "Absolute pointer data needs ASLR relocations")
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
