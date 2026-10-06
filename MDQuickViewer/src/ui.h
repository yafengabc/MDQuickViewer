/* 程序主入口：创建窗口、消息循环、退出。返回进程退出码。 */
#ifndef GOMD_UI_H
#define GOMD_UI_H

int ui_main(void);

/* 列表/预览宽度拆分：保证 listW>=0、prevW>=0，且 listW+prevW 不超过 cw。
 * 这是 resize 崩溃（负宽传给 MoveWindow）的根因防护，单独暴露供压测。 */
void split_list_preview(int cw, int panelW, int showList, int *listW, int *prevW);

/* 读取整个文件到 malloc 缓冲（末尾补 '\0'），失败返回 NULL。路径是 UTF-8，
 * 内部转 UTF-16 后用 _wfopen —— 这是中文路径能打开的关键，单独暴露供回归测试。 */
char *ui_read_file(const char *utf8_path, long *out_len);

/* 读取并加载一个 UTF-8 路径的 .md 文件。需要已创建主窗口。 */
void ui_load_file(const char *utf8_path);

#endif /* GOMD_UI_H */
