/* 検証用: ホストが提供しない関数を import するモジュール */
extern void no_such_host_function(void);
__attribute__((export_name("kb_scan"))) void kb_scan(void) { no_such_host_function(); }
