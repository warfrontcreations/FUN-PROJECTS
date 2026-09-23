# WhatsApp Assistant

A personal WhatsApp auto-reply bot. It connects to your WhatsApp account (via
[Baileys](https://github.com/WhiskeySockets/Baileys)), and for incoming DMs
generates a reply in your own texting style using Claude, then sends it back
automatically.

## How it works

- `index.js` connects to WhatsApp and listens for incoming messages.
- `lib/replyGenerator.js` sends the message (and image, if any) to Claude
  along with your style guide, and returns a reply.
- `persona.md` describes how you actually text — tone, slang, spelling
  habits, message length, emoji use, and real example messages. The more
  accurate this is, the more the replies sound like you instead of a generic
  assistant. This file is gitignored (it'll contain your real texting data
  and real contacts' names) — copy `persona.example.md` to `persona.md` and
  fill in your own style to get started.

## Features

- Replies to text messages, and to images (Claude looks at the image itself,
  not just the caption).
- Skips group chats by default (`SKIP_GROUPS`).
- Skips WhatsApp channel/newsletter broadcasts automatically.
- `ALLOWED_JIDS` — optionally restrict replies to only specific chats.
- `EXCLUDED_JIDS` — optionally exclude specific chats from auto-reply, even
  when everything else is allowed.
- `MODE=draft` — logs what it *would* reply without actually sending, so you
  can sanity-check behavior before going live with `MODE=send`.
- Guards against running two instances at once against the same session
  (`.bot.pid`), which otherwise causes WhatsApp connection conflicts.

## Setup

```bash
npm install
cp .env.example .env
# edit .env: add your ANTHROPIC_API_KEY, set MODE, etc.
npm start
```

On first run, scan the printed QR code with WhatsApp
(**Settings → Linked Devices → Link a Device**). Your session is then saved
to `auth_info/` so you don't need to re-scan on future runs.

## Configuration (`.env`)

| Variable | Purpose |
| --- | --- |
| `ANTHROPIC_API_KEY` | Your Claude API key |
| `MODEL` | Claude model used to generate replies |
| `MODE` | `draft` (log only) or `send` (actually reply) |
| `SKIP_GROUPS` | Skip group chats (default `true`) |
| `ALLOWED_JIDS` | Comma-separated allowlist of chat JIDs (empty = all) |
| `EXCLUDED_JIDS` | Comma-separated blocklist of chat JIDs to never reply to |

## Running

```bash
npm start          # start the bot
npm run stop        # stop it cleanly
npm run restart      # stop then start
```

For always-on hosting (so it keeps running even when your PC is off), see
the `pm2` scripts (`npm run pm2:start`, etc.) and deploy to a small VPS with
[pm2](https://pm2.keymetrics.io/) managing the process.

## Security notes

- `auth_info/` holds your live WhatsApp session — treat it like a password.
  It's gitignored and must never be committed or shared.
- `.env` holds your API key — also gitignored, never commit it.
