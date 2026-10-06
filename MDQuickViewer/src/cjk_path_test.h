#ifndef GOMD_CJK_PATH_TEST_H
#define GOMD_CJK_PATH_TEST_H

/* 中文（非 ASCII）路径回归测试，全通过返回 0。
 *
 * 覆盖「中文目录 + ASCII 文件名」「ASCII 目录 + 中文文件名」
 * 「中文目录 + 中文文件名」三种组合，走真实的 ui_read_file 代码路径。 */
int cjk_path_tests_run(void);

#endif
