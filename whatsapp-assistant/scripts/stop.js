/**
 * Stops the running whatsapp-assistant bot, if any, using the PID file
 * written by index.js at startup.
 */

const fs = require('fs');
const path = require('path');

const PID_FILE = path.join(__dirname, '..', '.bot.pid');

if (!fs.existsSync(PID_FILE)) {
  console.log('No .bot.pid file found — bot doesn\'t appear to be running.');
  process.exit(0);
}

const pid = parseInt(fs.readFileSync(PID_FILE, 'utf8').trim(), 10);

try {
  process.kill(pid, 0); // check it's alive before trying to kill it
} catch {
  console.log(`PID ${pid} from .bot.pid is not running (stale file) — cleaning up.`);
  fs.unlinkSync(PID_FILE);
  process.exit(0);
}

try {
  process.kill(pid, 'SIGTERM');
  console.log(`🛑 Stopped bot (PID ${pid}).`);
} catch (err) {
  console.error(`Failed to stop PID ${pid}:`, err.message);
  process.exit(1);
}

// index.js removes its own PID file on exit; give it a moment and clean up
// here too in case the process was killed too abruptly to run its handler.
setTimeout(() => {
  if (fs.existsSync(PID_FILE) && fs.readFileSync(PID_FILE, 'utf8').trim() === String(pid)) {
    fs.unlinkSync(PID_FILE);
  }
}, 1000);
