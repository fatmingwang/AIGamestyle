// @ts-check

// A Discord message → what to do. Most people use the menu; these text shortcuts skip it.
// The recipe format is ComfyLive's (ComfyLive/README.md "Recipes").
//
//   (anything else, or help)                 → the menu: buttons → forms (forms.mjs)
//   status | stop
//   <graph> [WxH] [key=value ...] <text>      e.g.  image 1024x1024 steps=30 a cute baby
//                                                   sfx seconds=2 a steel sword striking a shield
//                                                   voice voice="an old wizard, deep voice" So, another climber.
//   {"graph": "sfx", ...}                      a whole recipe as JSON (also inside a ```json block)
//   + an attached .json                        that recipe (key=value words in the message go on top)
//
// Values: numbers and true/false become JSON numbers/booleans; "quotes" keep spaces.

/** The graphs ComfyLive builds from a recipe, and which key the free text goes to. */
export const GRAPHS = /** @type {Record<string, string>} */ ({
  image: 'prompt',
  sfx: 'prompt',
  speech: 'text',
  voice: 'text',
  music: 'tags',
  video: 'prompt',
  'video-h3': 'prompt',
  'video-ltx': 'prompt',   // video with sound (LTX-2); sound="..." for what is heard
});

/**
 * @typedef {{ kind: 'menu' } | { kind: 'status' } | { kind: 'stop' } | { kind: 'error', message: string }
 *   | { kind: 'job', recipe: Record<string, any>, label: string, summary: string }} Parsed
 */

/** @param {string} s */
function toValue(s) {
  if (/^-?\d+(\.\d+)?$/.test(s)) return Number(s);
  if (/^(true|false)$/i.test(s)) return s.toLowerCase() === 'true';
  return s;
}

/** @param {string} s */
function stripFence(s) {
  const m = s.match(/^```(?:json)?\s*([\s\S]*?)\s*```$/i);
  return m ? m[1] : s;
}

/**
 * @param {string} textIn          the message without the bot mention
 * @param {string | null} attachedJson  the text of an attached .json, if any
 * @returns {Parsed}
 */
export function parseRequest(textIn, attachedJson) {
  let text = stripFence(textIn.trim());

  // 1. A whole recipe in the message
  if (text.startsWith('{')) {
    try {
      const recipe = JSON.parse(text);
      if (!recipe || typeof recipe !== 'object' || Array.isArray(recipe)) return { kind: 'error', message: 'The JSON must be an object: {"graph": "image", "prompt": "..."}' };
      return job(recipe);
    } catch (err) {
      return { kind: 'error', message: `That JSON does not parse: ${err instanceof Error ? err.message : err}` };
    }
  }

  const first = text.split(/\s+/, 1)[0].toLowerCase().replace(/:$/, '');
  if (!attachedJson && (text === '' || first === 'help' || first === '?' || first === 'menu')) return { kind: 'menu' };
  if (first === 'status' && text.split(/\s+/).length === 1) return { kind: 'status' };
  if ((first === 'stop' || first === 'cancel') && text.split(/\s+/).length === 1) return { kind: 'stop' };

  // 2. key=value words, and WxH
  /** @type {Record<string, any>} */
  const options = {};
  text = text.replace(/(^|\s)([A-Za-z_][\w/-]*)=(?:"([^"]*)"|'([^']*)'|(\S+))/g, (_all, lead, key, dq, sq, bare) => {
    options[key] = dq !== undefined ? dq : sq !== undefined ? sq : toValue(bare);
    return lead;
  });
  text = text.replace(/(^|\s)(\d{2,4})\s*[x×]\s*(\d{2,4})(?=\s|$)/i, (_all, lead, w, h) => {
    options.width ??= Number(w);
    options.height ??= Number(h);
    return lead;
  }).replace(/\s+/g, ' ').trim();

  // 3. An attached recipe: the message's options go on top
  if (attachedJson) {
    let recipe;
    try {
      recipe = JSON.parse(attachedJson.replace(/^﻿/, ''));
    } catch (err) {
      return { kind: 'error', message: `The attached .json does not parse: ${err instanceof Error ? err.message : err}` };
    }
    if (!recipe || typeof recipe !== 'object' || Array.isArray(recipe)) return { kind: 'error', message: 'The attached .json must be an object.' };
    return job({ ...recipe, ...options });
  }

  // 4. <graph> <text>; anything else gets the menu
  const words = text.split(' ');
  const named = words[0]?.toLowerCase().replace(/:$/, '') ?? '';
  if (!(named in GRAPHS)) return { kind: 'menu' };
  const graph = named;
  const rest = words.slice(1).join(' ').trim();
  const textKey = GRAPHS[graph];
  /** @type {Record<string, any>} */
  const recipe = { graph, ...options };
  if (rest) recipe[textKey] = rest;
  if (recipe[textKey] === undefined && recipe.text === undefined && recipe.prompt === undefined)
    return { kind: 'error', message: `What should I make? e.g. \`${graph} ${graph === 'speech' || graph === 'voice' ? 'Hello, traveller.' : 'a cute baby'}\`` };
  return job(recipe);
}

/** @param {Record<string, any>} recipe  @returns {Parsed} */
function job(recipe) {
  const graph = typeof recipe.graph === 'string' ? recipe.graph : '';
  // no "graph": a song file, a settings file or an API graph (ComfyLive tells them apart)
  const label = (graph || (recipe.lyrics || recipe.tags ? 'music' : 'recipe')).replace(/[^\w-]/g, '') || 'recipe';
  const what = [recipe.prompt, recipe.text, recipe.tags, recipe.name].find(v => typeof v === 'string' || Array.isArray(v));
  const summary = what === undefined ? '' : (Array.isArray(what) ? what.join(', ') : String(what));
  return { kind: 'job', recipe, label, summary };
}

export const HELP_TEXT = [
  '**Text commands**: the same as the menu forms, in one message.',
  '```',
  'image a cute baby                       a picture (default size of the graph)',
  'image 256x256 steps=20 a cute baby      size and any graph input as key=value',
  'sfx seconds=2 a steel sword striking a metal shield',
  'speech language=English speaker=Ryan People of the valley, hear me.',
  'voice voice="an old wizard, deep and raspy" So, another climber.',
  'music duration=60 bpm=90 lofi hip hop, mellow piano',
  'video seconds=3 a young archer draws a bow   (+ attach a picture = first frame)',
  'video-ltx seconds=4 sound="a loud thud" a knight hits a dummy   video WITH sound',
  '{"graph": "sfx", "prompt": "...", "seconds": 1.5}   a whole recipe as JSON',
  '```',
  'Or attach a recipe `.json` (any file from ComfyLive/examples). Any other message shows the menu.',
  '`status`: queue and server · `stop`: stop your job · DM me, or @mention me in the bot channel.',
].join('\n');
