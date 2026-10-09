const path = require('path');
const { execFile } = require('child_process');
const vscode = require('vscode');
const commandSaving = new Set();

const actionTitles = {
  run: 'Запуск Simpl',
  compile: 'Компиляция Simpl',
  check: 'Проверка Simpl',
  repl: 'Simpl REPL'
};

function activeSimplDocument() {
  const editor = vscode.window.activeTextEditor;
  const fileName = editor && editor.document.fileName.toLowerCase();
  if (!editor || editor.document.isUntitled
      || (!fileName.endsWith('.sim') && !fileName.endsWith('.simpl'))) {
    vscode.window.showErrorMessage('Откройте сохранённый файл .sim или .simpl.');
    return undefined;
  }
  return editor.document;
}

function getExecutable() {
  return vscode.workspace.getConfiguration('simpl').get('path', 'simpl');
}

function getCwd(document) {
  const folder = document && vscode.workspace.getWorkspaceFolder(document.uri);
  return folder ? folder.uri.fsPath : document ? path.dirname(document.fileName) : process.cwd();
}

function runCli(args, cwd) {
  return new Promise((resolve, reject) => {
    execFile(getExecutable(), args, { cwd, encoding: 'utf8', maxBuffer: 1024 * 1024 },
      (error, stdout, stderr) => {
        if (error) {
          error.cliOutput = stderr.trim() || stdout.trim() || error.message;
          reject(error);
        } else {
          resolve({ stdout, stderr });
        }
      });
  });
}

function createTask(action, document, extraArgs = []) {
  const folder = document && vscode.workspace.getWorkspaceFolder(document.uri);
  const fileName = document && document.fileName;
  const args = action === 'repl'
    ? ['repl']
    : [action, fileName, ...extraArgs];
  const execution = new vscode.ProcessExecution(getExecutable(), args, { cwd: getCwd(document) });
  return new vscode.Task(
    { type: 'simpl', action, file: fileName },
    folder || vscode.TaskScope.Global,
    actionTitles[action] || `${action} Simpl`,
    'Simpl',
    execution,
    ['$simpl']
  );
}

async function saveDocument(document) {
  const fileName = path.resolve(document.fileName);
  commandSaving.add(fileName);
  try {
    return await document.save();
  } finally {
    commandSaving.delete(fileName);
  }
}

async function executeDocumentTask(action, extraArgs = []) {
  const document = activeSimplDocument();
  if (!document || !(await saveDocument(document))) return;
  await vscode.tasks.executeTask(createTask(action, document, extraArgs));
}

async function runWithArguments() {
  const input = await vscode.window.showInputBox({
    prompt: 'Аргументы программы в формате JSON-массива строк',
    placeHolder: '["первый", "второй аргумент"]',
    value: '[]',
    validateInput: value => {
      try {
        const args = JSON.parse(value);
        return Array.isArray(args) && args.every(arg => typeof arg === 'string')
          ? undefined : 'Введите JSON-массив строк.';
      } catch (_) {
        return 'Введите корректный JSON-массив строк.';
      }
    }
  });
  if (input === undefined) return;
  await executeDocumentTask('run', JSON.parse(input));
}

async function compileAs() {
  const document = activeSimplDocument();
  if (!document) return;
  const defaultOutput = document.fileName.replace(/\.(?:sim|simpl)$/i, '.simc');
  const outputPath = await vscode.window.showInputBox({
    prompt: 'Путь для скомпилированного файла',
    value: defaultOutput
  });
  if (!outputPath || !(await saveDocument(document))) return;
  await vscode.tasks.executeTask(createTask('compile', document, ['-o', outputPath]));
}

async function showOutput(title, content) {
  const document = await vscode.workspace.openTextDocument({
    language: 'plaintext',
    content
  });
  await vscode.window.showTextDocument(document, { preview: false, viewColumn: vscode.ViewColumn.Beside });
  vscode.window.setStatusBarMessage(title, 3000);
}

async function showDisassembly(fileName, options = []) {
  const bytecodePath = fileName.replace(/\.(?:sim|simpl)$/i, '.simc');
  try {
    const { stdout } = await runCli(['disasm', ...options, bytecodePath], getCwd({
      uri: vscode.Uri.file(fileName),
      fileName
    }));
    await showOutput('Байткод Simpl', stdout);
  } catch (error) {
    vscode.window.showErrorMessage(error.cliOutput);
  }
}

async function formatDocument() {
  const document = activeSimplDocument();
  if (!document || !(await saveDocument(document))) return;
  try {
    await runCli(['fmt', document.fileName], getCwd(document));
    const bytes = await vscode.workspace.fs.readFile(document.uri);
    const formatted = Buffer.from(bytes).toString('utf8');
    const edit = new vscode.WorkspaceEdit();
    edit.replace(document.uri,
      new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)),
      formatted);
    if (await vscode.workspace.applyEdit(edit)) {
      await saveDocument(document);
      vscode.window.setStatusBarMessage('Файл Simpl отформатирован', 3000);
    }
  } catch (error) {
    vscode.window.showErrorMessage(error.cliOutput || error.message);
  }
}

async function extractDocs() {
  const document = activeSimplDocument();
  if (!document || !(await saveDocument(document))) return;
  try {
    const { stdout } = await runCli(['doc', document.fileName], getCwd(document));
    await showOutput('Документация Simpl', stdout);
  } catch (error) {
    vscode.window.showErrorMessage(error.cliOutput);
  }
}

async function createProject() {
  const folder = vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
  const base = folder ? folder.uri.fsPath : process.cwd();
  const name = await vscode.window.showInputBox({
    prompt: 'Имя нового проекта Simpl',
    value: 'simpl-project',
    validateInput: value => value.trim() ? undefined : 'Укажите имя проекта.'
  });
  if (!name) return;
  const destination = path.resolve(base, name.trim());
  try {
    const { stdout } = await runCli(['new', destination], base);
    vscode.window.showInformationMessage(stdout.trim() || `Проект создан: ${destination}`);
  } catch (error) {
    vscode.window.showErrorMessage(error.cliOutput);
  }
}

async function chooseDisassemblyOptions(document) {
  const selected = await vscode.window.showQuickPick([
    { label: 'Обычный вывод', description: 'Имена переменных отображаются' },
    { label: 'Скрыть имена', description: 'Передать --no-names', option: '--no-names' },
    { label: 'Показать статистику', description: 'Передать --stats', option: '--stats' },
    { label: 'Без имён и со статистикой', option: 'both' }
  ], { placeHolder: 'Настройки дизассемблера' });
  if (!selected) return;
  const options = selected.option === 'both'
    ? ['--no-names', '--stats']
    : selected.option ? [selected.option] : [];
  await showDisassembly(document.fileName, options);
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand('simpl.run', () => executeDocumentTask('run')),
    vscode.commands.registerCommand('simpl.runWithArguments', runWithArguments),
    vscode.commands.registerCommand('simpl.compile', () => executeDocumentTask('compile')),
    vscode.commands.registerCommand('simpl.compileAs', compileAs),
    vscode.commands.registerCommand('simpl.check', () => executeDocumentTask('check')),
    vscode.commands.registerCommand('simpl.format', formatDocument),
    vscode.commands.registerCommand('simpl.doc', extractDocs),
    vscode.commands.registerCommand('simpl.new', createProject),
    vscode.commands.registerCommand('simpl.repl', () =>
      vscode.tasks.executeTask(createTask('repl', undefined))),
    vscode.commands.registerCommand('simpl.help', async () => {
      try {
        const { stdout } = await runCli(['--help'], getCwd());
        await showOutput('Справка Simpl CLI', stdout);
      } catch (error) {
        vscode.window.showErrorMessage(error.cliOutput);
      }
    }),
    vscode.commands.registerCommand('simpl.version', async () => {
      try {
        const { stdout } = await runCli(['--version'], getCwd());
        vscode.window.showInformationMessage(stdout.trim());
      } catch (error) {
        vscode.window.showErrorMessage(error.cliOutput);
      }
    }),
    vscode.commands.registerCommand('simpl.disasm', async () => {
      const document = activeSimplDocument();
      if (!document || !(await saveDocument(document))) return;
      await showDisassembly(document.fileName);
    }),
    vscode.commands.registerCommand('simpl.disasmOptions', async () => {
      const document = activeSimplDocument();
      if (!document || !(await saveDocument(document))) return;
      await chooseDisassemblyOptions(document);
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
