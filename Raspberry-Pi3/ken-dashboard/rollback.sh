#!/usr/bin/env bash
# Restore a KEN Dashboard version saved by install.sh.
#   sudo bash rollback.sh               newest backup in /var/backups/ken-dashboard
#   sudo bash rollback.sh <backup-dir>  a specific backup
set -euo pipefail
if [[ "$EUID" -ne 0 ]]; then
  echo 'Chạy: sudo bash rollback.sh [thư-mục-sao-lưu]'; exit 1
fi
ken_backup="${1:-}"
if [[ -z "$ken_backup" ]]; then
  ken_backup="$(ls -1dt /var/backups/ken-dashboard/*/ 2>/dev/null | head -n 1 || true)"
fi
ken_backup="${ken_backup%/}"
if [[ -z "$ken_backup" || ! -f "$ken_backup/www/index.html" ]]; then
  echo 'Không tìm thấy bản sao lưu hợp lệ trong /var/backups/ken-dashboard'; exit 1
fi
printf 'Khôi phục từ: %s\n' "$ken_backup"
install -d -m 755 /var/www/ken-dashboard
find /var/www/ken-dashboard -mindepth 1 -delete
cp -a "$ken_backup/www/." /var/www/ken-dashboard/
if [[ -f "$ken_backup/config_server.py" ]]; then
  install -m 755 "$ken_backup/config_server.py" /usr/local/lib/ken-dashboard/config_server.py
fi
systemctl restart ken-dashboard-config.service
systemctl restart nginx
printf 'Đã khôi phục giao diện. Cấu hình dùng chung giữ nguyên: /var/lib/ken-dashboard/config.json\n'
if [[ -f "$ken_backup/config.json" ]]; then
  printf 'Muốn lấy lại cả cấu hình dùng chung lúc sao lưu:\n  sudo install -o www-data -g www-data -m 640 %s/config.json /var/lib/ken-dashboard/config.json\n  rồi tải lại trang trên mọi máy.\n' "$ken_backup"
fi
