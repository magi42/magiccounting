# Magic Counting

Magic Counting is a Qt Widgets double-accounting app. The main window edits account transactions in a table. Each transaction has a date, a source account, an amount, the other party, one or more target account splits, a memo, and computed posting columns for every configured account.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/magiccounting
```

The project targets C++17 and Qt 5 Widgets/SQL.

You can pass the accounting file as an optional command-line argument:

```sh
./build/magiccounting /path/to/books.maccd
```

The UI uses the system locale by default. Finnish localization is included and is loaded automatically for Finnish system locales. Use `Language` in the menu bar to choose System default, English, or Finnish; the choice is stored in the application configuration file.

## Data Files

The primary data file is now a SQLite database with the `.maccd` extension. It stores accounts, parties, transactions, and target splits in relational tables, so editing one transaction row can update only that transaction instead of rewriting the whole accounting document.

The readable JSON format with the `.macc` extension is still supported. Use `File -> Save As...` and choose a `.macc` path when you want a portable text snapshot of the same accounting data.

Use `File -> Open Accounting File...` to load another `.maccd` database or `.macc` JSON file. Transaction edits, account changes, parties, and opening balances are written back to the active file. Use `File -> Save As...` to write the current accounting data to a different `.maccd` or `.macc` file and make that file active.

Use `File -> Import Bank Statement...` to import S-Pankki account transactions from a semicolon-separated `.csv` file. The import asks which bookkeeping account represents the bank account. Negative statement rows are posted from that bank account to a counter account, and positive rows are posted from the counter account to the bank account. Imported rows store the statement source and archive identifier, so importing the same CSV again skips already imported transactions.

The default counter account for imported rows is `Luokittelemattomat`. Use `Edit -> Import Classification Rules...` to configure case-insensitive text rules for the other party, for example mapping a grocery store name fragment to a food account.

The last opened accounting file is stored in the application configuration file `config.json` under Qt's application config folder, and is reopened on the next startup when no command-line file is provided. If no previous file exists, the app asks for a `.maccd` file name at startup.

Legacy folders containing `accounts.json`, `parties.json`, and `transactions.json` can still be opened by passing the folder path on the command line; the app then saves the data into `accounting.maccd` inside that folder.

Click or double-click the `Target accounts` cell to edit split rows in a popup. The popup keeps `OK` disabled until the target split total equals the transaction `Amount`. The source account posting is shown as a negative amount, and negative postings are rendered in red.

The first row is an opening-balance row. Enter each account's initial balance directly in that row; values are saved in the active accounting file. The app inserts a generated balance row after the last transaction of each month, showing each account's balance after that month.
