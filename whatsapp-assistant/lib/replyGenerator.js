const fs = require('fs');
const path = require('path');
const Anthropic = require('@anthropic-ai/sdk');

const anthropic = new Anthropic({ apiKey: process.env.ANTHROPIC_API_KEY });
const MODEL = process.env.MODEL || 'claude-haiku-4-5-20251001';

let personaCache = null;
function loadPersona() {
  if (personaCache) return personaCache;
  const personaPath = path.join(__dirname, '..', 'persona.md');
  personaCache = fs.existsSync(personaPath)
    ? fs.readFileSync(personaPath, 'utf8')
    : 'Reply casually and briefly, like a normal person texting.';
  return personaCache;
}

// Simple in-memory rolling history per chat, so replies have context.
// Lost on restart — fine for now, can move to a file/DB later if needed.
const history = new Map(); // jid -> [{ role, content }]
const MAX_HISTORY = 10;

function pushHistory(jid, role, content) {
  const h = history.get(jid) || [];
  h.push({ role, content });
  while (h.length > MAX_HISTORY) h.shift();
  history.set(jid, h);
}

async function generateReply(jid, incomingText, image) {
  const userContent = image
    ? [
        { type: 'image', source: { type: 'base64', media_type: image.mimeType, data: image.base64 } },
        { type: 'text', text: incomingText || '(no caption)' },
      ]
    : incomingText;

  pushHistory(jid, 'user', userContent);

  const systemPrompt = `You are ghostwriting WhatsApp replies for a real person, matching their exact texting style.

Style guide (as described by the person you're writing for):
${loadPersona()}

Rules:
- Reply as if YOU are them, in first person. Never say you are an AI or assistant, never break character.
- Keep it short — match typical WhatsApp message length, not essay-length.
- Match their tone, punctuation habits, and vocabulary from the style guide above.
- If the message asks something you can't actually know (plans, commitments, personal facts not in the style guide), keep the reply vague/noncommittal rather than inventing specifics.
- If the incoming message includes an image, react to what's actually in it, briefly, the way the person would casually react to a photo/video sent by a friend.
- If the incoming text is a placeholder like "[sent a video]" (video content isn't visible to you, only noted), reply casually to the fact that a video was sent, without pretending to know what's in it.
- Output ONLY the reply text — no quotes, no explanation, no labels.`;

  const response = await anthropic.messages.create({
    model: MODEL,
    max_tokens: 200,
    system: systemPrompt,
    messages: history.get(jid),
  });

  const reply = response.content
    .filter((block) => block.type === 'text')
    .map((block) => block.text)
    .join('')
    .trim();

  pushHistory(jid, 'assistant', reply);
  return reply;
}

module.exports = { generateReply };
