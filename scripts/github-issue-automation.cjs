'use strict';

const defaultConfig = require('../.github/issue-automation.json');

function labelNames(issue) {
  return (issue.labels || []).map(label => typeof label === 'string' ? label : label.name);
}

function withoutExamples(text) {
  return (text || '')
    .replace(/<!--[\s\S]*?-->/g, '')
    .replace(/^\s*(`{3,}|~{3,})[^\n]*\n[\s\S]*?^\s*\1\s*$/gm, '');
}

function narrative(text) {
  return withoutExamples((text || '')
    .replace(/<details\b[^>]*>[\s\S]*?<\/details>/gi, '')
    .replace(/^#{1,6}\s+Automatic installation report\s*$/gmi, ''));
}

function classifyIssue(issue, config = defaultConfig) {
  if (issue.pull_request || labelNames(issue).includes(config.skipLabel)) return [];
  // The automatic report lists all calibration profiles and installed drivers.
  // Classifying that boilerplate would label every report with every model/area.
  const controllerState = (issue.body || '').match(/^Controller state:[^\r\n]*/mi)?.[0] || '';
  const text = `${issue.title || ''}\n${narrative(issue.body)}\n${controllerState}`
    .normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
  const existing = labelNames(issue);
  const types = ['bug', 'enhancement', 'question'];
  const hasManualType = existing.some(label => types.includes(label));
  const controllerRules = config.rules.filter(rule => rule.category === 'controller');
  // Prefer an explicit title/controller-state model over incidental comparisons.
  const modelSource = `${issue.title || ''}\n${controllerState}`;
  const hasExplicitModel = controllerRules.some(rule => new RegExp(rule.pattern, 'i').test(modelSource));
  return config.rules.filter(rule => {
    if (hasManualType && types.includes(rule.label)) return false;
    const source = hasExplicitModel && rule.category === 'controller' ? modelSource : text;
    return new RegExp(rule.pattern, 'i').test(source);
  }).map(rule => rule.label);
}

function referencedIssues(body, owner, repo) {
  const numbers = new Set();
  const sameRepo = (candidateOwner, candidateRepo) =>
    candidateOwner.toLowerCase() === owner.toLowerCase() && candidateRepo.toLowerCase() === repo.toLowerCase();
  const add = value => {
    const number = Number(value);
    if (Number.isSafeInteger(number) && number > 0) numbers.add(number);
  };
  // Process link destinations rather than '#123' link captions pointing elsewhere.
  let text = withoutExamples(body).replace(/\[[^\]]*\]\((https?:\/\/[^\s)]+)(?:\s+"[^"]*")?\)/gi, ' $1 ');
  text = text.replace(/https?:\/\/[^\s<>\])]+/gi, url => {
    const match = url.match(/^https:\/\/github\.com\/([^/]+)\/([^/]+)\/issues\/(\d+)(?:[/?#]|$)/i);
    if (match && sameRepo(match[1], match[2])) add(match[3]);
    return ' ';
  });
  text = text.replace(/\b([\w.-]+)\/([\w.-]+)#(\d+)\b/g, (_, candidateOwner, candidateRepo, number) => {
    if (sameRepo(candidateOwner, candidateRepo)) add(number);
    return ' ';
  });
  for (const match of text.matchAll(/(?<![\w/#?=&])#(\d+)\b/g)) add(match[1]);
  return [...numbers].sort((a, b) => a - b);
}

async function ensureLabel(github, repository, name, config) {
  const definition = config.labels[name];
  if (!definition) throw new Error(`Missing label definition: ${name}`);
  try {
    await github.rest.issues.getLabel({ ...repository, name });
  } catch (error) {
    if (error.status !== 404) throw error;
    try {
      await github.rest.issues.createLabel({ ...repository, name, ...definition });
    } catch (createError) {
      // Another issue workflow may have created the same label in parallel.
      if (createError.status !== 422) throw createError;
      await github.rest.issues.getLabel({ ...repository, name });
    }
  }
}

async function labelIssue({ github, context, config = defaultConfig }) {
  // Fetch current content, not an outdated event snapshot after rapid edits.
  const repository = context.repo;
  const issue_number = context.payload.issue.number;
  const { data: issue } = await github.rest.issues.get({ ...repository, issue_number });
  const existing = labelNames(issue);
  const labels = classifyIssue(issue, config).filter(label => !existing.includes(label));
  for (const label of labels) await ensureLabel(github, repository, label, config);
  if (labels.length) await github.rest.issues.addLabels({ ...repository, issue_number, labels });
  return labels;
}

function releaseComment(issue, release, marker) {
  const version = String(release.tag_name).replace(/[\r\n`]/g, '');
  return `${marker}\n` +
    `@${issue.user.login}, a change related to this issue is available in **${version}**` +
    `${release.prerelease ? ' (pre-release)' : ''}. This issue is now **pending testing**.\n\n` +
    `**Download:** [${version}](${release.html_url})\n\n` +
    `Please install this version and repeat the steps from your original report. ` +
    `Reply here to confirm whether the problem is resolved, including the version tested, ` +
    `your APEX model and USB/dongle connection.\n\n` +
    `If the problem persists, describe what still happens and attach fresh \`tray_bridge.log\` ` +
    `and \`tray_detection.log\` files after checking them for personal information.\n\n` +
    `The issue will stay open while awaiting testing; the maintainer will review your feedback.`;
}

async function followRelease({ github, context, core, tag, config = defaultConfig }) {
  const repository = context.repo;
  const tagName = tag || context.payload.release?.tag_name;
  if (!tagName) throw new Error('A release tag is required.');
  const { data: release } = await github.rest.repos.getReleaseByTag({ ...repository, tag: tagName });
  if (release.draft || !release.published_at) {
    core.info('Draft/unpublished release: no issue follow-up.');
    return [];
  }
  const numbers = referencedIssues(release.body, repository.owner, repository.repo);
  const marker = `<!-- asb-release-testing:${release.id} -->`;
  const updated = [];
  const failures = [];
  for (const issue_number of numbers) {
    try {
      let issue;
      try {
        ({ data: issue } = await github.rest.issues.get({ ...repository, issue_number }));
      } catch (error) {
        if (error.status !== 404 && error.status !== 410) throw error;
        core.warning(`Skipping unavailable issue #${issue_number}.`);
        continue;
      }
      const labels = labelNames(issue);
      if (issue.pull_request || issue.locked || issue.state_reason === 'not_planned' ||
          labels.includes(config.skipLabel) || config.excludedReleaseLabels.some(label => labels.includes(label))) {
        core.info(`Skipping PR, locked, excluded or not-planned issue #${issue_number}.`);
        continue;
      }
      const comments = await github.paginate(github.rest.issues.listComments, {
        ...repository, issue_number, per_page: 100
      });
      if (comments.some(comment => comment.user?.login === 'github-actions[bot]' && comment.body?.includes(marker))) {
        core.info(`Issue #${issue_number} already notified for ${tagName}.`);
        continue;
      }
      await ensureLabel(github, repository, config.pendingTestingLabel, config);
      // A merge may have auto-closed a fixed issue before a release existed.
      if (issue.state === 'closed') {
        await github.rest.issues.update({ ...repository, issue_number, state: 'open' });
      }
      await github.rest.issues.addLabels({ ...repository, issue_number, labels: [config.pendingTestingLabel] });
      for (const name of config.supersededStatusLabels.filter(label => labels.includes(label))) {
        try {
          await github.rest.issues.removeLabel({ ...repository, issue_number, name });
        } catch (error) {
          if (error.status !== 404) throw error;
        }
      }
      // Post last: a retry repairs a partial label update without duplicate messages.
      await github.rest.issues.createComment({ ...repository, issue_number, body: releaseComment(issue, release, marker) });
      updated.push(issue_number);
    } catch (error) {
      core.warning(`Issue #${issue_number}: ${error.message}`);
      failures.push(issue_number);
    }
  }
  core.info(`Release ${tagName}: pending testing for ${updated.map(number => `#${number}`).join(', ') || 'no new issues'}.`);
  if (failures.length) throw new Error(`Release follow-up failed for: ${failures.map(number => `#${number}`).join(', ')}. Rerun this workflow to retry.`);
  return updated;
}

module.exports = { classifyIssue, referencedIssues, labelIssue, followRelease };
