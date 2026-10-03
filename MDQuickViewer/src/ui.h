/* 程序主入口：创建窗口、消息循环、退出。返回进程退出码。 */
#ifndef GOMD_UI_H
#define GOMD_UI_H

int ui_main(void);

/* 列表/预览宽度拆分：保证 listW>=0、prevW>=0，且 listW+prevW 不超过 cw。
 * 这是 resize 崩溃（负宽传给 MoveWindow）的根因防护，单独暴露供压测。 */
void split_list_preview(int cw, int panelW, int showList, int *listW, int *prevW);

#endif /* GOMD_UI_H */
