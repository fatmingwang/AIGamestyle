// @ts-check
import * as fs from 'node:fs';
import * as path from 'node:path';

// The menu (one button per graph) and the form (Discord modal, up to 5 fields) behind each button.
// A submitted form becomes a ComfyLive recipe (ComfyLive/README.md "Recipes"). An empty field is left out, so the
// graph's own default is used.
//
// custom_id:  cl:open:<graph>    a menu button → its form
//             cl:submit:<graph>  the form's Submit
//             cl:status  cl:help  cl:stop
// ("cl:" only: the build machine, which may share the bot, uses "bm:" and ignores these.)

export const COLOR = { green: 0x2ecc71, red: 0xe74c3c, orange: 0xffa500, blue: 0x3498db, purple: 0x9b59b6 };
export const EPHEMERAL = 1 << 6;
const BUTTON = { primary: 1, secondary: 2, success: 3, danger: 4 };

/**
 * @typedef {{ id: string, label: string, kind?: 'text' | 'int' | 'number' | 'size', paragraph?: boolean,
 *   required?: boolean, placeholder?: string, remember?: boolean, choices?: string[], wrongChoice?: string }} Field
 * @typedef {{ label: string, emoji: string, title: string, fields: Field[],
 *   check?: (recipe: Record<string, any>) => string | null }} Form   check: what is wrong with the whole recipe, or null
 */

// Qwen3-TTS's lists (ComfyLive/README.md "Speech"): any other value is refused by the graph, so the form checks first
const SPEAKERS = ['Ryan', 'Aiden', 'Vivian', 'Serena', 'Uncle_fu', 'Dylan', 'Eric', 'Ono_anna', 'Sohee'];
const LANGUAGES = ['Chinese', 'English', 'Japanese', 'Korean', 'German', 'French', 'Russian', 'Portuguese', 'Spanish', 'Italian'];

/** One form per graph. Field ids are recipe keys ("size" → width + height). Max 5 fields, labels ≤ 45 chars. */
/** @type {Record<string, Form>} */
export const FORMS = {
  image: {
    label: 'Image', emoji: '🖼️', title: 'Image: text to picture',
    fields: [
      { id: 'prompt', label: 'What to draw', paragraph: true, required: true, placeholder: 'a cute baby, smiling, soft pastel background' },
      { id: 'avoid', label: 'Avoid (negative prompt)', paragraph: true, placeholder: 'blurry, text, watermark, extra fingers' },
      { id: 'size', label: 'Size (width x height)', kind: 'size', placeholder: '1024x1024 (empty: 1024x1024)' },
      { id: 'steps', label: 'Steps', kind: 'int', placeholder: "empty: the graph's default" },
      { id: 'seed', label: 'Seed (empty: random)', kind: 'int', remember: false, placeholder: 'the same seed + values = the same picture' },
    ],
  },
  sfx: {
    label: 'Sound effect', emoji: '🔊', title: 'Sound effect',
    fields: [
      { id: 'prompt', label: 'What it sounds like', paragraph: true, required: true, placeholder: 'a steel sword striking a metal shield, sharp metallic clang, single hit' },
      { id: 'avoid', label: 'Avoid', paragraph: true, placeholder: 'music, voices, noise' },
      { id: 'seconds', label: 'Length in seconds', kind: 'number', placeholder: '2' },
      { id: 'steps', label: 'Steps', kind: 'int', placeholder: "empty: the graph's default" },
      { id: 'seed', label: 'Seed (empty: random)', kind: 'int', remember: false },
    ],
  },
  speech: {
    label: 'Speech', emoji: '🗣️', title: 'Speech: a preset voice',
    fields: [
      { id: 'text', label: 'What to say', paragraph: true, required: true, placeholder: 'People of the valley, hear me. The hero is already climbing.' },
      { id: 'speaker', label: 'Speaker (one of the 9 names)', placeholder: SPEAKERS.join(', '), choices: SPEAKERS,
        wrongChoice: 'To describe a new voice ("a 2 year old boy"), use the 🎙️ Voice form instead.' },
      { id: 'language', label: 'Language', placeholder: LANGUAGES.join(', '), choices: LANGUAGES },
      { id: 'style', label: 'Tone / style', placeholder: 'a proud old king, slow and solemn' },
      { id: 'seed', label: 'Seed (empty: random)', kind: 'int', remember: false },
    ],
  },
  voice: {
    label: 'Voice', emoji: '🎙️', title: 'Voice: a described voice',
    fields: [
      { id: 'text', label: 'What to say', paragraph: true, required: true, placeholder: 'So, another climber.' },
      { id: 'voice', label: 'The voice', paragraph: true, required: true, placeholder: 'An old male wizard, deep and slightly raspy voice, speaking slowly' },
      { id: 'language', label: 'Language', placeholder: LANGUAGES.join(', '), choices: LANGUAGES },
      { id: 'temperature', label: 'Temperature', kind: 'number', placeholder: "empty: the graph's default" },
      { id: 'seed', label: 'Seed (empty: random)', kind: 'int', remember: false },
    ],
  },
  music: {
    label: 'Music', emoji: '🎵', title: 'Music (ACE-Step)',
    fields: [
      { id: 'tags', label: 'Style tags', paragraph: true, required: true, placeholder: 'lofi hip hop, mellow piano, vinyl crackle, relaxed' },
      { id: 'lyrics', label: 'Lyrics', paragraph: true, placeholder: '[Verse 1]\nEvery evening when the lights turn low\n...' },
      { id: 'duration', label: 'Length in seconds', kind: 'number', placeholder: '60' },
      { id: 'bpm', label: 'BPM', kind: 'int', placeholder: '90' },
      { id: 'keyscale', label: 'Key', placeholder: 'C major, Bb major, A minor...' },
    ],
  },
  video: {
    label: 'Video', emoji: '🎬', title: 'Video: text to video',
    fields: [
      { id: 'prompt', label: 'What happens', paragraph: true, required: true, placeholder: 'a young archer in a blue tunic stands facing right, draws a wooden bow and releases the arrow' },
      { id: 'avoid', label: 'Avoid', paragraph: true, placeholder: "empty: the built-in negative prompt" },
      { id: 'seconds', label: 'Length in seconds', kind: 'number', placeholder: '3' },
      { id: 'size', label: 'Size (width x height)', kind: 'size', placeholder: '1280x704 (empty: the graph default)' },
      { id: 'seed', label: 'Seed (empty: random)', kind: 'int', remember: false },
    ],
  },
  // LTX-2 (ComfyLive's "video-ltx"): one MP4 with picture and sound. On a 16 GB card 768x512 for 4 s uses 15.5 GB,
  // so the form keeps a job at or under that many pixels x seconds.
  'video-ltx': {
    label: 'Video + sound', emoji: '🎞️', title: 'Video with sound (LTX-2)',
    fields: [
      { id: 'prompt', label: 'What you see', paragraph: true, required: true, placeholder: 'a knight in silver armor swings a sword at a wooden training dummy in a castle courtyard' },
      { id: 'sound', label: 'What you hear (say it plainly)', paragraph: true, placeholder: 'a sharp metallic swoosh, then a loud wooden thud, the knight grunts' },
      { id: 'seconds', label: 'Length in seconds (1 to 6)', kind: 'number', placeholder: '4' },
      { id: 'size', label: 'Size (width x height)', kind: 'size', placeholder: '768x512 (or 512x768; bigger = shorter)' },
      { id: 'seed', label: 'Seed (empty: random)', kind: 'int', remember: false },
    ],
    check: recipe => {
      const seconds = recipe.seconds ?? 4;
      if (seconds < 1 || seconds > 6) return `**Length**: ${seconds} s is outside 1 to 6 seconds.`;
      const w = recipe.width ?? 768, h = recipe.height ?? 512;
      if (w * h * seconds > 768 * 512 * 4.5)
        return `**Size x length**: ${w}x${h} for ${seconds} s is too much for the server's 16 GB card. `
          + 'Keep it at about 768x512 for 4 s: smaller for longer clips, shorter for bigger ones.';
      return null;
    },
  },
};

/** @param {string} s  @param {number} n */
const cut = (s, n) => (s.length > n ? s.slice(0, n - 1) + '…' : s);
/** @param {...unknown} components */
const row = (...components) => ({ type: 1, components });
/** @param {string} id  @param {string} label  @param {number} style  @param {string} [emoji] */
const button = (id, label, style, emoji) => ({ type: 2, custom_id: id, label, style, ...(emoji ? { emoji: { name: emoji } } : {}) });

/** The menu that answers any message. */
export function menuMessage() {
  const graphs = Object.entries(FORMS).map(([g, f]) => button(`cl:open:${g}`, f.label, g === 'image' ? BUTTON.primary : BUTTON.secondary, f.emoji));
  return {
    embeds: [{
      title: '🎨 What should I make?',
      description: 'Pick one, fill in the form and press **Submit**. The result (MP4, MP3 or PNG) comes back here.\n'
        + '**Video** is silent (Wan 2.2); **Video + sound** makes the picture and its sound together (LTX-2, about 1-2 min).\n'
        + 'Video with a first-frame picture: send `video <what happens>` with the picture attached.',
      color: COLOR.purple,
    }],
    components: [
      row(...graphs.slice(0, 5)),
      row(...graphs.slice(5), button('cl:status', 'Status', BUTTON.secondary, '📊'), button('cl:help', 'Text commands', BUTTON.secondary, '❔')),
    ],
  };
}

/** The Stop button on a running job's status message. */
export const stopRow = () => [row(button('cl:stop', 'Stop', BUTTON.danger, '✖️'))];

/** A button that opens the form again (after a wrong value).  @param {string} graph */
export const editRow = graph => [row(button(`cl:open:${graph}`, 'Edit and try again', BUTTON.primary, '✏️'))];

/** The form for a graph, filled in with the person's last values.  @param {string} graph  @param {Record<string, string>} last */
export function formModal(graph, last) {
  const form = FORMS[graph];
  return {
    type: 9,
    data: {
      custom_id: `cl:submit:${graph}`,
      title: cut(`${form.emoji} ${form.title}`, 45),
      components: form.fields.map(f => row({
        type: 4,
        custom_id: f.id,
        label: cut(f.label, 45),
        style: f.paragraph ? 2 : 1,
        required: !!f.required,
        max_length: f.paragraph ? 4000 : 200,
        ...(f.placeholder ? { placeholder: cut(f.placeholder, 100) } : {}),
        ...(last[f.id] ? { value: last[f.id] } : {}),
      })),
    },
  };
}

/**
 * The submitted fields → a recipe, or what is wrong.
 * @param {string} graph  @param {Record<string, string>} fields
 * @returns {{ ok: true, recipe: Record<string, any>, label: string, summary: string } | { ok: false, message: string }}
 */
export function formToRecipe(graph, fields) {
  const form = FORMS[graph];
  if (!form) return { ok: false, message: `Unknown graph "${graph}"` };
  /** @type {Record<string, any>} */
  const recipe = { graph };
  const problems = [];
  for (const f of form.fields) {
    const v = (fields[f.id] ?? '').trim();
    if (!v) {
      if (f.required) problems.push(`**${f.label}** is needed.`);
      continue;
    }
    if (f.kind === 'int') {
      if (!/^-?\d+$/.test(v)) problems.push(`**${f.label}**: "${v}" is not a whole number.`);
      else recipe[f.id] = Number(v);
    } else if (f.kind === 'number') {
      if (!/^-?\d+(\.\d+)?$/.test(v)) problems.push(`**${f.label}**: "${v}" is not a number.`);
      else recipe[f.id] = Number(v);
    } else if (f.kind === 'size') {
      const m = v.match(/^(\d{2,4})\s*[x×*,]\s*(\d{2,4})$/i);
      if (!m) problems.push(`**${f.label}**: "${v}" should look like 1024x1024.`);
      else { recipe.width = Number(m[1]); recipe.height = Number(m[2]); }
    } else if (f.choices) {
      // any case, and Uncle Fu = Uncle_fu; written back as the graph spells it
      const key = (/** @type {string} */ x) => x.toLowerCase().replace(/[\s_-]+/g, '');
      const hit = f.choices.find(c => key(c) === key(v));
      if (!hit) problems.push(`**${f.label}**: "${v}" is not one of: ${f.choices.join(', ')}.${f.wrongChoice ? ' ' + f.wrongChoice : ''}`);
      else recipe[f.id] = hit;
    } else {
      recipe[f.id] = v;
    }
  }
  if (!problems.length && form.check) {
    const wrong = form.check(recipe);
    if (wrong) problems.push(wrong);
  }
  if (problems.length) return { ok: false, message: problems.join('\n') };
  const main = form.fields.find(f => f.required);
  return { ok: true, recipe, label: graph, summary: main ? String(recipe[main.id] ?? '') : '' };
}

/** Each person's last form values per graph (seeds are not kept), in data/formValues.json. */
export class FormStore {
  /** @param {string} file */
  constructor(file) {
    this.file = file;
    /** @type {Record<string, Record<string, Record<string, string>>>} */
    this.data = {};
    try { this.data = JSON.parse(fs.readFileSync(file, 'utf8')); } catch { /* first run */ }
  }

  /** @param {string} userId  @param {string} graph */
  get(userId, graph) { return this.data[userId]?.[graph] ?? {}; }

  /** @param {string} userId  @param {string} graph  @param {Record<string, string>} fields */
  set(userId, graph, fields) {
    const keep = Object.fromEntries((FORMS[graph]?.fields ?? []).filter(f => f.remember !== false && fields[f.id]?.trim()).map(f => [f.id, fields[f.id]]));
    (this.data[userId] ??= {})[graph] = keep;
    try {
      fs.mkdirSync(path.dirname(this.file), { recursive: true });
      fs.writeFileSync(this.file, JSON.stringify(this.data, null, 2));
    } catch { /* only the prefill is lost */ }
  }
}
