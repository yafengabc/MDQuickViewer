/* 注册表：记忆上次打开的目录（HKCU\Software\MDQuickViewer\LastFolder，REG_SZ）。
 *
 * 测试可用环境变量 MDQV_REG_KEY 把子键
 * 重定向到 Software\MDQuickViewer_test，避免污染真实配置、也避免多个测试相互干扰。
 *
 * 所有读取函数返回指向本模块静态缓冲区的指针，调用方不要 free，
 * 也不要在再次调用同一函数之后长期持有（会被覆盖）。 */
#ifndef GOMD_SETTINGS_H
#define GOMD_SETTINGS_H

/* 读取上次打开的目录；记录不存在或目录已失效时返回 ""（静态缓冲）。 */
const char *settings_load_last_folder(void);

/* 写入目录；空值或非目录直接忽略。 */
void settings_save_last_folder(const char *dir);

/* 清除记录（测试用）。 */
void settings_delete_last_folder(void);

/* 初始目录解析：cur（内存当前目录）> last（注册表）> exe 目录 > "." */
const char *settings_initial_folder(const char *cur);

#endif /* GOMD_SETTINGS_H */
