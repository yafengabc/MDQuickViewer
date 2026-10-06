/* 文件 / 文件夹选择对话框。
 * 返回 malloc 的 UTF-8 路径；用户取消返回 NULL，调用方需 free。 */
#ifndef GOMD_DLG_H
#define GOMD_DLG_H

char *dlg_open_file(const char *init_dir);
char *dlg_open_folder(const char *init_dir);

#endif /* GOMD_DLG_H */
