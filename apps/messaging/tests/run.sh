#!/bin/sh
# Host checks only: actual portable store/blob/sync and app/service/portal code, no device access.
set -eu
cd "$(dirname "$0")/../../.."
msg_out=$(mktemp -d "${TMPDIR:-/tmp}/badge-messaging-store.XXXXXX")
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I components/os/messages -I components/os/include \
  apps/messaging/tests/test_messaging.c components/os/messages/message_store.c \
  components/os/messages/message_sync.c components/os/messages/message_blob.c \
  -o "$msg_out/store"
"$msg_out/store"
python3 apps/messaging/tests/test_messaging_service.py
python3 apps/messaging/tests/test_messaging_integration.py
python3 apps/messaging/tests/test_messaging_factory.py
python3 apps/messaging/tests/test_messaging_http.py
python3 apps/messaging/tests/test_message_portal_session.py
node apps/messaging/tests/test_messaging_portal.js
python3 apps/messaging/tests/test_messaging_ui.py
