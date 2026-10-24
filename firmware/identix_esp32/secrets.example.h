#ifndef SECRETS_H
#define SECRETS_H

// ---------------------------------------------------------------------
// 1. Copy this file to "secrets.h" in the same folder.
// 2. Fill in your own values below.
// 3. "secrets.h" is listed in .gitignore, so it will never be committed.
// ---------------------------------------------------------------------

#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Local backend that receives scan results (PHP/XAMPP server on your machine).
// Use your computer's LAN IP address, e.g. "192.168.1.42" — find it with
// `ipconfig` (Windows) or `ifconfig` / `ip addr` (macOS/Linux).
#define BACKEND_HOST "192.168.1.42"
#define BACKEND_PATH "/identix/save_data.php"

#endif
