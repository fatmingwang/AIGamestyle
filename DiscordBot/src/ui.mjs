// @ts-check
import { EPHEMERAL, FORMS, editRow, formModal, formToRecipe } from './forms.mjs';
import { HELP_TEXT } from './request.mjs';

// What a click on the bot's buttons or a submitted form does (custom_ids: forms.mjs).
// Discord needs an answer within 3 s, so anything slower is acknowledged first and filled in afterwards.

const CB = { message: 4, deferredMessage: 5, modal: 9 };

export class BotUi {
  /**
   * @param {import('./jobs.mjs').JobRunner} jobs
   * @param {import('./discordRest.mjs').DiscordRest} rest
   * @param {import('./forms.mjs').FormStore} store
   * @param {import('./logger.mjs').Logger} log
   */
  constructor(jobs, rest, store, log) {
    this.jobs = jobs;
    this.rest = rest;
    this.store = store;
    this.log = log;
  }

  /** @param {import('./discordGateway.mjs').BotInteraction} x */
  async handle({ i, user, customId, fields, inDM }) {
    const [, action, graph] = customId.split(':');

    // a menu button (or "Edit and try again") → the form, with the person's last values
    if (action === 'open' && graph in FORMS) {
      return this.answer(i, formModal(graph, this.store.get(user.id, graph)));
    }

    // Submit → check the values → queue the job
    if (action === 'submit' && graph in FORMS) {
      this.store.set(user.id, graph, fields);   // kept even when wrong: "Edit and try again" starts from them
      const r = formToRecipe(graph, fields);
      if (!r.ok) {
        return this.answer(i, { type: CB.message, data: { flags: EPHEMERAL, content: `❌ Please fix:\n${r.message}`, components: editRow(graph) } });
      }
      await this.answer(i, { type: CB.message, data: { flags: EPHEMERAL, content: `✅ Submitted: **${FORMS[graph].label}**. The progress and the result come below.` } });
      /** @type {import('./discordGateway.mjs').BotRequest} */
      const req = { user, channelId: i.channel_id, messageId: i.message?.id ?? '', inDM, text: '', attachments: [] };
      await this.jobs.enqueue(req, r);
      return;
    }

    if (action === 'help') {
      return this.answer(i, { type: CB.message, data: { flags: EPHEMERAL, content: HELP_TEXT } });
    }

    // slower ones: "thinking..." first, then the answer
    if (action === 'status' || action === 'stop') {
      if (!(await this.answer(i, { type: CB.deferredMessage, data: { flags: EPHEMERAL } }))) return;
      const text = action === 'status' ? await this.jobs.statusText() : await this.jobs.stopFor(user);
      const r = await this.rest.editOriginal(i, { content: text });
      if (!r.ok) this.log.error(`Discord: could not answer ${customId} (${r.status}):`, r.data);
      return;
    }

    this.log.log(`Discord: unknown button ${customId}`);
    await this.answer(i, { type: CB.message, data: { flags: EPHEMERAL, content: 'This button is no longer used. Send me any message for a new menu.' } });
  }

  /** @param {any} i  @param {object} body  @returns {Promise<boolean>} */
  async answer(i, body) {
    const r = await this.rest.callback(i, body);
    if (!r.ok) this.log.error(`Discord: interaction answer failed (${r.status}):`, r.data);
    return r.ok;
  }
}
