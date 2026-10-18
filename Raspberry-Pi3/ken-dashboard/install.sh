#!/usr/bin/env bash
set -euo pipefail
if [[ "$EUID" -ne 0 ]]; then
  echo 'Chạy: sudo bash install.sh'; exit 1
fi
ken_source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
for ken_file in index.html style.css themes.css ha.js direct-speaker.js app.js config_server.py; do
  test -f "$ken_source_dir/$ken_file" || { echo "Thiếu file: $ken_file"; exit 1; }
done
test -f "$ken_source_dir/assets/xeko.png" || { echo 'Thiếu file: assets/xeko.png'; exit 1; }
for ken_theme in spring summer autumn winter; do
  for ken_detail in backdrop card panel; do
    test -f "$ken_source_dir/assets/seasons/$ken_theme-$ken_detail.svg" || { echo "Thiếu họa tiết: $ken_theme-$ken_detail.svg"; exit 1; }
  done
done
for ken_theme in default dark; do
  test -f "$ken_source_dir/assets/seasons/$ken_theme-panel.svg" || { echo "Thiếu họa tiết: $ken_theme-panel.svg"; exit 1; }
done
apt-get update
apt-get install -y nginx python3
install -d -m 755 /var/www/ken-dashboard
for ken_file in index.html style.css themes.css ha.js direct-speaker.js app.js; do
  install -m 644 "$ken_source_dir/$ken_file" "/var/www/ken-dashboard/$ken_file"
done
install -d -m 755 /var/www/ken-dashboard/assets
install -m 644 "$ken_source_dir/assets/xeko.png" /var/www/ken-dashboard/assets/xeko.png
install -d -m 755 /var/www/ken-dashboard/assets/seasons
for ken_theme in spring summer autumn winter; do
  for ken_detail in backdrop card panel; do
    install -m 644 "$ken_source_dir/assets/seasons/$ken_theme-$ken_detail.svg" "/var/www/ken-dashboard/assets/seasons/$ken_theme-$ken_detail.svg"
  done
done
for ken_theme in default dark; do
  install -m 644 "$ken_source_dir/assets/seasons/$ken_theme-panel.svg" "/var/www/ken-dashboard/assets/seasons/$ken_theme-panel.svg"
done
install -d -m 755 /usr/local/lib/ken-dashboard
install -m 755 "$ken_source_dir/config_server.py" /usr/local/lib/ken-dashboard/config_server.py
install -d -o www-data -g www-data -m 750 /var/lib/ken-dashboard
cat > /etc/systemd/system/ken-dashboard-config.service <<'SERVICE'
[Unit]
Description=KEN Dashboard shared configuration API
After=network.target

[Service]
Type=simple
User=www-data
Group=www-data
ExecStart=/usr/bin/python3 /usr/local/lib/ken-dashboard/config_server.py
Restart=on-failure
RestartSec=2
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/ken-dashboard

[Install]
WantedBy=multi-user.target
SERVICE
cat > /etc/nginx/sites-available/ken-dashboard <<'NGINX'
server {
    listen 8080;
    listen [::]:8080;
    server_name _;
    root /var/www/ken-dashboard;
    index index.html;
    add_header X-Content-Type-Options nosniff always;
    add_header Referrer-Policy no-referrer always;
    add_header Cache-Control "no-store" always;
    location = /api/config {
        client_max_body_size 2m;
        proxy_pass http://127.0.0.1:8091/api/config;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_read_timeout 10s;
    }
    location / { try_files $uri $uri/ =404; }
}
NGINX
ln -sfn /etc/nginx/sites-available/ken-dashboard /etc/nginx/sites-enabled/ken-dashboard
nginx -t
systemctl daemon-reload
systemctl enable --now ken-dashboard-config.service
systemctl enable nginx
systemctl restart nginx
printf '\nĐã cài. Trên iPad mở: http://IP_CUA_PI3:8080\n'
printf 'IP của Pi: '
hostname -I
printf 'Cấu hình dùng chung: /var/lib/ken-dashboard/config.json\n'
