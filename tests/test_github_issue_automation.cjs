'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { classifyIssue, referencedIssues, labelIssue, followRelease } = require('../scripts/github-issue-automation.cjs');

const repository = { owner: 'ReynArts', repo: 'ApexSenseBridge' };
const makeIssue = (number, extra = {}) => ({
  number, title: '[Bug] APEX 5 trigger crash', body: '', state: 'open', labels: [],
  user: { login: 'reporter' }, ...extra
});

function fixture(issues = [makeIssue(44)], releaseExtra = {}) {
  const byNumber = new Map(issues.map(issue => [issue.number, structuredClone(issue)]));
  const comments = new Map();
  const labels = new Set();
  const calls = [];
  const release = {
    id: 1234, tag_name: 'v1.0.3', body: 'Fix controller recovery (#44).',
    html_url: 'https://github.com/ReynArts/ApexSenseBridge/releases/tag/v1.0.3',
    draft: false, published_at: '2026-11-04T12:00:00Z', prerelease: false, ...releaseExtra
  };
  const github = {
    rest: {
      repos: { getReleaseByTag: async () => ({ data: release }) },
      issues: {
        get: async ({ issue_number }) => {
          if (!byNumber.has(issue_number)) throw Object.assign(new Error('Not found'), { status: 404 });
          return { data: structuredClone(byNumber.get(issue_number)) };
        },
        getLabel: async ({ name }) => {
          if (!labels.has(name)) throw Object.assign(new Error('Not found'), { status: 404 });
          return { data: { name } };
        },
        createLabel: async ({ name }) => { labels.add(name); calls.push(['createLabel', name]); },
        addLabels: async ({ issue_number, labels: added }) => {
          const issue = byNumber.get(issue_number);
          issue.labels = [...new Set([...issue.labels, ...added])];
          calls.push(['addLabels', issue_number, added]);
        },
        removeLabel: async ({ issue_number, name }) => {
          const issue = byNumber.get(issue_number);
          issue.labels = issue.labels.filter(label => label !== name);
          calls.push(['removeLabel', issue_number, name]);
        },
        update: async ({ issue_number, state }) => {
          byNumber.get(issue_number).state = state;
          calls.push(['update', issue_number, state]);
        },
        listComments: async () => {},
        createComment: async ({ issue_number, body }) => {
          const entries = comments.get(issue_number) || [];
          entries.push({ body, user: { login: 'github-actions[bot]' } });
          comments.set(issue_number, entries);
          calls.push(['createComment', issue_number, body]);
        }
      }
    },
    paginate: async (method, { issue_number }) => {
      assert.equal(method, github.rest.issues.listComments);
      return comments.get(issue_number) || [];
    }
  };
  return {
    github, context: { repo: repository, payload: { issue: { number: 44 }, release } },
    core: { info() {}, warning() {} }, byNumber, comments, calls, labels, release
  };
}

test('labels French reports by model and component, with accents', () => {
  assert.deepEqual(classifyIssue(makeIssue(44, { title: '[Bug] Gâchettes APEX-4 et vibrations' })),
    ['bug', 'apex4', 'triggers', 'haptics']);
});

test('the app diagnostic boilerplate does not tag every model, driver and feature', () => {
  const body = '## Description\nMy controller disconnects.\n## Automatic installation report\n' +
    '<details><summary>Installation diagnostics</summary>\n```text\n' +
    'HidHide registered version: 1.5.230\nMode: Installer\n' +
    'Controller state: APEX 6 Pro connected\n' +
    '--apex4-gyro 100 --apex5-rumble 100 --apex6-haptic-gain 100\n' +
    'Adaptive triggers and haptic feedback\n```\n</details>';
  assert.deepEqual(classifyIssue(makeIssue(44, { title: '[Bug] Disconnect', body })), ['bug', 'apex6']);
});

test('an environment setup heading or an installed dependency is not an installer failure', () => {
  assert.deepEqual(classifyIssue(makeIssue(44, {
    title: '[Bug] APEX 5 inputs ignored', body: 'Setup\nGameInput installed. Also tried the Playnite extension.'
  })), ['bug', 'apex5', 'playnite']);
  assert.ok(classifyIssue(makeIssue(44, { title: 'Driver install error' })).includes('installer'));
});

test('preserves maintainer type labels and prefers the explicitly reported controller', () => {
  assert.deepEqual(classifyIssue(makeIssue(44, {
    title: '[Bug] APEX  4 touchpad mapping', labels: ['enhancement'],
    body: 'The APEX 5 is mentioned only for comparison.'
  })), ['apex4', 'touchpad']);
});

test('ignores comments and fenced log examples when classifying', () => {
  assert.deepEqual(classifyIssue(makeIssue(44, {
    title: 'Question', body: '<!-- apex 4 -->\n```text\nAPEX 5 trigger crash\n```'
  })), ['question']);
});

test('labels the three report categories sent by the application', () => {
  assert.deepEqual(classifyIssue(makeIssue(44, { title: '[Bug] Overlay stays open', body: '## Description\nIt never closes.' })), ['bug']);
  assert.deepEqual(classifyIssue(makeIssue(44, { title: '[Feature request] Per-game profiles', body: '## Request\nA profile per game.' })), ['enhancement']);
  assert.deepEqual(classifyIssue(makeIssue(44, {
    title: '[Unsupported game] Hades II', body: '## Game\n\n- Name: Hades II\n- Executable or store link: Hades2.exe'
  })), ['game-request']);
});

test('honours opt-out labels and ignores pull requests', () => {
  assert.deepEqual(classifyIssue(makeIssue(44, { labels: [{ name: 'automation:skip' }] })), []);
  assert.deepEqual(classifyIssue(makeIssue(44, { pull_request: {} })), []);
});

test('adds missing labels without replacing manual labels, and is safe to rerun', async () => {
  const f = fixture([makeIssue(44, { labels: ['priority:high', 'bug'] })]);
  assert.deepEqual(await labelIssue(f), ['apex5', 'triggers']);
  assert.ok(f.byNumber.get(44).labels.includes('priority:high'));
  const before = f.calls.length;
  assert.deepEqual(await labelIssue(f), []);
  assert.equal(f.calls.length, before);
});

test('recognises local references, deduplicates them and ignores foreign repositories', () => {
  const body = [
    '#44, #44; ReynArts/ApexSenseBridge#45',
    '[#46](https://github.com/ReynArts/ApexSenseBridge/issues/46)',
    'https://github.com/reynarts/apexsensebridge/issues/47#issuecomment-123',
    'other/repo#999; [#998](https://github.com/other/repo/issues/998)',
    'https://example.com/?x=#997 https://github.com/other/repo/issues/996#995',
    'https://github.com/ReynArts/ApexSenseBridge/pull/994',
    '<!-- #993 -->', '```text\n#992\n```',
    '<details><summary>Fixes</summary>\n#48\n</details>', '#0 #9007199254740993'
  ].join('\n');
  assert.deepEqual(referencedIssues(body, repository.owner, repository.repo), [44, 45, 46, 47, 48]);
});

test('published release moves issues to pending testing and posts an actionable comment', async () => {
  const f = fixture([makeIssue(44, { labels: ['needs-info', 'triggers', 'priority:high'] })]);
  assert.deepEqual(await followRelease(f), [44]);
  assert.deepEqual(f.byNumber.get(44).labels, ['triggers', 'priority:high', 'pending testing']);
  const body = f.comments.get(44)[0].body;
  assert.match(body, /@reporter/);
  assert.match(body, /v1\.0\.3/);
  assert.ok(body.includes(f.release.html_url));
  assert.match(body, /repeat the steps/);
  assert.match(body, /tray_bridge\.log/);
  assert.equal(f.byNumber.get(44).state, 'open');
});

test('retries never duplicate notifications or reset a status after testing', async () => {
  const f = fixture();
  await followRelease(f);
  f.byNumber.get(44).state = 'closed';
  f.byNumber.get(44).labels = ['tested'];
  const before = f.calls.length;
  assert.deepEqual(await followRelease(f), []);
  assert.equal(f.calls.length, before);
  assert.equal(f.comments.get(44).length, 1);
  assert.equal(f.byNumber.get(44).state, 'closed');
});

test('a different release can request a fresh test on the same issue', async () => {
  const f = fixture();
  await followRelease(f);
  Object.assign(f.release, { id: 5678, tag_name: 'v1.0.4' });
  assert.deepEqual(await followRelease(f), [44]);
  assert.equal(f.comments.get(44).length, 2);
});

test('reopens merge-closed completed issues, skips rejected/locked/excluded issues and PRs', async () => {
  const f = fixture([
    makeIssue(44, { state: 'closed', state_reason: 'completed' }),
    makeIssue(45, { state: 'closed', state_reason: 'not_planned' }),
    makeIssue(46, { pull_request: {} }), makeIssue(47, { locked: true }),
    makeIssue(48, { labels: ['automation:skip'] }), makeIssue(49, { labels: ['duplicate'] })
  ], { body: '#44 #45 #46 #47 #48 #49 #404' });
  assert.deepEqual(await followRelease(f), [44]);
  assert.equal(f.byNumber.get(44).state, 'open');
  assert.equal(f.comments.size, 1);
});

test('does nothing for drafts and clearly identifies pre-releases', async () => {
  const draft = fixture(undefined, { draft: true, published_at: null });
  assert.deepEqual(await followRelease(draft), []);
  assert.equal(draft.calls.length, 0);
  const prerelease = fixture(undefined, { prerelease: true, tag_name: 'v1.0.4-beta.1' });
  await followRelease(prerelease);
  assert.match(prerelease.comments.get(44)[0].body, /pre-release/);
});

test('a reporter cannot forge the bot notification marker to block follow-up', async () => {
  const f = fixture();
  f.comments.set(44, [{ body: '<!-- asb-release-testing:1234 -->', user: { login: 'reporter' } }]);
  assert.deepEqual(await followRelease(f), [44]);
  assert.equal(f.comments.get(44).length, 2);
});

test('partial failures do not block other issues, and reruns repair only unfinished work', async () => {
  const f = fixture([makeIssue(44), makeIssue(45)], { body: '#44 #45' });
  const createComment = f.github.rest.issues.createComment;
  f.github.rest.issues.createComment = async args => {
    if (args.issue_number === 44) throw new Error('Temporary API failure');
    return createComment(args);
  };
  await assert.rejects(followRelease(f), /#44/);
  assert.equal(f.comments.get(45).length, 1);
  f.github.rest.issues.createComment = createComment;
  assert.deepEqual(await followRelease(f), [44]);
  assert.equal(f.comments.get(44).length, 1);
  assert.equal(f.comments.get(45).length, 1);
});
