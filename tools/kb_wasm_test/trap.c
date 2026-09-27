/* 検証用: kb_scan で trap するモジュール */
__attribute__((export_name("kb_scan"))) void kb_scan(void) { __builtin_trap(); }
