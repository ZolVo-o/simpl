const path = require('path');
const { execFile } = require('child_process');
const vscode = require('vscode');
const commandSaving = new Set();

function activeSimplDocument() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.isUntitled || !editor.document.fileName.toLowerCase().endsWith('.sim')) {
    vscode.window.showErrorMessage('Откройте сохранённый файл .sim.');
    return undefined;
  }
  return editor.document;
}

function createTask(action, document) {
  const executable = vscode.workspace.getConfiguration('simpl').get('path', 'simpl');
  const folder = vscode.workspace.getWorkspaceFolder(document.uri);
  const cwd = folder ? folder.uri.fsPath : path.dirname(document.fileName);
  const definition = { type: 'simpl', action, file: document.fileName };
  const args = action === 'compile'
    ? ['compile', document.fileName]
    : ['run', document.fileName];
  const execution = new vscode.ProcessExecution(executable, args, { cwd });
  return new vscode.Task(
    definition,
    folder || vscode.TaskScope.Global,
    action === 'compile' ? 'Компиляция Simpl' : 'Запуск Simpl',
    'Simpl',
    execution,
    ['$simpl']
  );
}

async function saveAndRun(action) {
  const document = activeSimplDocument();
  if (!document) return;
  const fileName = path.resolve(document.fileName);
  commandSaving.add(fileName);
  try {
    if (!(await document.save())) return;
  } finally {
    commandSaving.delete(fileName);
  }
  await vscode.tasks.executeTask(createTask(action, document));
}

async function showDisassembly(fileName) {
  const executable = vscode.workspace.getConfiguration('simpl').get('path', 'simpl');
  const bytecodePath = fileName.replace(/\.sim$/i, '.simc');
  const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(fileName));
  const cwd = folder ? folder.uri.fsPath : path.dirname(fileName);
  execFile(executable, ['disasm', bytecodePath], { cwd, encoding: 'utf8' }, async (error, stdout, stderr) => {
    if (error) {
      vscode.window.showErrorMessage(stderr.trim() || error.message);
      return;
    }
    const document = await vscode.workspace.openTextDocument({
      language: 'plaintext',
      content: stdout
    });
    await vscode.window.showTextDocument(document, { preview: false });
  });
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand('simpl.run', () => saveAndRun('run')),
    vscode.commands.registerCommand('simpl.compile', () => saveAndRun('compile')),
    vscode.commands.registerCommand('simpl.disasm', async () => {
      const document = activeSimplDocument();
      if (!document) return;
      if (!(await document.save())) return;
      showDisassembly(document.fileName);
    }),
    vscode.tasks.onDidEndTaskProcess(event => {
      const definition = event.execution.task.definition;
      if (definition.type !== 'simpl' || definition.action !== 'compile' || event.exitCode !== 0) return;
      if (vscode.workspace.getConfiguration('simpl').get('showBytecode', false)) {
        showDisassembly(definition.file);
      }
    }),
    vscode.workspace.onDidSaveTextDocument(document => {
      if (document.languageId === 'simpl'
          && !commandSaving.has(path.resolve(document.fileName))
          && vscode.workspace.getConfiguration('simpl').get('autoCompileOnSave', false)) {
        vscode.tasks.executeTask(createTask('compile', document));
      }
    })
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
