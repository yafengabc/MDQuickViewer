package main

import (
	"os"
	"path/filepath"
	"testing"
)

// TestLastFolderPersist 验证"记住上次打开的目录"（注册表存储）的读写闭环与容错。
func TestLastFolderPersist(t *testing.T) {
	// 重定向到测试专用子键，避免污染真实配置
	t.Setenv("GOMD_REG_KEY", `Software\gomd_test`)
	deleteLastFolder()
	t.Cleanup(deleteLastFolder)

	if got := loadLastFolder(); got != "" {
		t.Fatalf("初始记录应为空, got %q", got)
	}

	dir := t.TempDir()
	saveLastFolder(dir)
	if got := loadLastFolder(); got != dir {
		t.Fatalf("读写闭环失败: got %q want %q", got, dir)
	}

	saveLastFolder("")                           // 空值：忽略
	saveLastFolder(filepath.Join(dir, "不存在XYZ")) // 无效目录：忽略
	if got := loadLastFolder(); got != dir {
		t.Fatalf("空值/无效目录不应覆盖记录: got %q", got)
	}

	// initialFolder 优先级：内存 dlgFolder > 注册表记录 > exe 目录
	prev := dlgFolder
	dlgFolder = ""
	if got := initialFolder(); got != dir {
		t.Fatalf("initialFolder 应回退到注册表记录: got %q want %q", got, dir)
	}
	dlgFolder = dir
	if got := initialFolder(); got != dir {
		t.Fatalf("initialFolder 应优先用内存当前目录: got %q", got)
	}
	dlgFolder = prev

	// 目录被删除后记录应视为失效
	os.RemoveAll(dir)
	dlgFolder = ""
	if got := loadLastFolder(); got != "" {
		t.Fatalf("目录失效后记录应返回空: got %q", got)
	}
}
