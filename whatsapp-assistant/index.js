/**
 * WhatsApp Assistant — Step 2: auto-reply
 *
 * Connects to WhatsApp, and for incoming DMs generates a reply in your
 * style using Claude. Starts in MODE=draft (logs the reply but doesn't
 * send it) so you can sanity-check it before flipping to MODE=send.
 */

require('dotenv').config();

const {
  default: makeWASocket,
  useMultiFileAuthState,
  DisconnectReason,
  downloadMediaMessage,
  getContentType,
} = require('@whiskeysockets/baileys');
const { Boom } = require('@hapi/boom');
const qrcode = require('qrcode-terminal');
const pino = require('pino');
const { generateReply } = require('./lib/replyGenerator');

const fs = require('fs');
const path = require('path');

const PID_FILE = path.join(__dirname, '.bot.pid');

// If another instance's PID file exists and that process is still alive,
// refuse to start — running two instances against the same auth_info
// causes WhatsApp connection conflicts. Skipped under pm2, which already
// guarantees a single instance and may leave a stale PID file behind
// after an abrupt crash/restart.
if (!process.env.pm_id && fs.existsSync(PID_FILE)) {
  const existingPid = parseInt(fs.readFileSync(PID_FILE, 'utf8').trim(), 10);
  let alive = false;
  try {
    process.kill(existingPid, 0); // signal 0 = check without killing
    alive = true;
  } catch {
    alive = false;
  }
  if (alive) {
    console.error(`❌ Bot already running (PID ${existingPid}). Run "npm run stop" first, or delete .bot.pid if that's stale.`);
    process.exit(1);
  }
}

fs.writeFileSync(PID_FILE, String(process.pid));

function cleanupPidFile() {
  try {
    if (fs.readFileSync(PID_FILE, 'utf8').trim() === String(process.pid)) {
      fs.unlinkSync(PID_FILE);
    }
  } catch {
    // already gone, nothing to do
  }
}

process.on('exit', cleanupPidFile);
process.on('SIGINT', () => process.exit());
process.on('SIGTERM', () => process.exit());

const MODE = (process.env.MODE || 'draft').toLowerCase(); // 'draft' or 'send'
const SKIP_GROUPS = process.env.SKIP_GROUPS !== 'false'; // default true
const ALLOWED_JIDS = (process.env.ALLOWED_JIDS || '')
  .split(',')
  .map((s) => s.trim())
  .filter(Boolean); // empty = allow all chats
const EXCLUDED_JIDS = (process.env.EXCLUDED_JIDS || '')
  .split(',')
  .map((s) => s.trim())
  .filter(Boolean); // chats to never auto-reply to, even if ALLOWED_JIDS is empty

async function startBot() {
  const { state, saveCreds } = await useMultiFileAuthState('auth_info');

  const sock = makeWASocket({
    auth: state,
    logger: pino({ level: 'silent' }),
  });

  sock.ev.on('connection.update', (update) => {
    const { connection, lastDisconnect, qr } = update;

    if (qr) {
      console.log('\nScan this QR code with WhatsApp:');
      console.log('  Phone > Settings > Linked Devices > Link a Device\n');
      qrcode.generate(qr, { small: true });
    }

    if (connection === 'close') {
      const statusCode = new Boom(lastDisconnect?.error)?.output?.statusCode;
      const loggedOut = statusCode === DisconnectReason.loggedOut;

      console.log(`⚠️  Connection closed (statusCode=${statusCode}): ${lastDisconnect?.error?.message || 'unknown'}`);

      if (loggedOut) {
        console.log('❌ Logged out. Delete the "auth_info" folder and restart to link again.');
      } else {
        console.log('   Reconnecting in 3s...');
        setTimeout(startBot, 3000);
      }
    } else if (connection === 'open') {
      console.log(`✅ Connected to WhatsApp! Mode: ${MODE.toUpperCase()}. Listening...\n`);
      if (MODE !== 'send') {
        console.log('(Running in DRAFT mode — replies are logged, not sent. Set MODE=send in .env to go live.)\n');
      }
    }
  });

  sock.ev.on('creds.update', saveCreds);

  sock.ev.on('messages.upsert', async ({ messages, type }) => {
    if (type !== 'notify') return;

    for (const msg of messages) {
      if (!msg.message || msg.key.fromMe) continue;

      const jid = msg.key.remoteJid;
      if (jid.endsWith('@newsletter') || jid === 'status@broadcast') continue; // channels/status, not real chats

      const isGroup = jid.endsWith('@g.us');
      const messageType = getContentType(msg.message);

      let text = msg.message.conversation || msg.message.extendedTextMessage?.text || '';
      let image = null;

      if (messageType === 'imageMessage') {
        text = msg.message.imageMessage.caption || '';
        console.log(`📩 ${jid}: [image]${text ? ' ' + text : ''}`);
      } else if (messageType === 'videoMessage') {
        // No video understanding via the Claude API — reply based on caption only.
        text = msg.message.videoMessage.caption || '[sent a video]';
        console.log(`📩 ${jid}: [video]${msg.message.videoMessage.caption ? ' ' + msg.message.videoMessage.caption : ''}`);
      } else if (!text) {
        continue; // skip other non-text types for now (stickers, audio, documents, etc.)
      } else {
        console.log(`📩 ${jid}: ${text}`);
      }

      if (isGroup && SKIP_GROUPS) continue;
      if (EXCLUDED_JIDS.includes(jid)) continue;
      if (ALLOWED_JIDS.length > 0 && !ALLOWED_JIDS.includes(jid)) continue;

      try {
        if (messageType === 'imageMessage') {
          const buffer = await downloadMediaMessage(msg, 'buffer', {}, {
            logger: pino({ level: 'silent' }),
            reuploadRequest: sock.updateMediaMessage,
          });
          image = { base64: buffer.toString('base64'), mimeType: msg.message.imageMessage.mimetype };
        }

        const reply = await generateReply(jid, text, image);

        if (MODE === 'send') {
          await sock.sendMessage(jid, { text: reply });
          console.log(`✉️  Sent to ${jid}: ${reply}\n`);
        } else {
          console.log(`✏️  [DRAFT — not sent] Would reply to ${jid}: ${reply}\n`);
        }
      } catch (err) {
        console.error('Error generating/sending reply:', err.message);
      }
    }
  });
}

startBot().catch((err) => {
  console.error('Fatal error starting bot:', err);
});
