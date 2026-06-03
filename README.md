# Magic Counting

Magic Counting is a Qt Widgets double-accounting app. The main window edits account transactions in a table. Each transaction has booked and paid dates, a source account, an amount, the other party, one or more target account splits, a memo, and computed posting columns for every configured account.

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

Use `Edit -> Parties...` to assign a default account to a transaction counterpart. Exact counterpart defaults are applied before the broader import classification rules. When an account is renamed in `Edit -> Accounts...`, transactions, counterpart defaults, and import classification rules that reference that account are updated to the new name.
Use `Edit -> Classify Unclassified` to apply the current counterpart defaults and import classification rules to transactions whose source or counterpart account is still `Luokittelemattomat`.

The last opened accounting file is stored in the application configuration file `config.json` under Qt's application config folder, and is reopened on the next startup when no command-line file is provided. If no previous file exists, the app asks for a `.maccd` file name at startup.

Legacy folders containing `accounts.json`, `parties.json`, and `transactions.json` can still be opened by passing the folder path on the command line; the app then saves the data into `accounting.maccd` inside that folder.

Double-click a transaction row, or click its `Target accounts` cell, to edit the full transaction details in one dialog. The dialog includes the memo, receipt preview, and counterpart account split rows, and keeps `OK` disabled until the target split total equals the transaction `Amount`. The memo is not shown as a separate table column. The source account posting is shown as a negative amount, and negative postings are rendered in red.

Receipt files are stored as file paths, not as bytes in the database. Drop an image file, typically a JPEG receipt scan, or a PDF receipt from outside the application onto a transaction row or onto the receipt panel in the transaction dialog to attach it. Paths under the accounting file folder are saved relative to that folder.
PDF receipts are previewed by rendering the first page to a temporary JPEG with Poppler's `pdftoppm` command when it is available.

Click the `Booked` or `Paid` column header to order transactions by the booked or paid date. Monthly balance rows are still calculated from booked dates, so a transaction booked in one month and paid in the next is included in the booked month balance before the later paid date appears.

Account posting columns can be reordered by dragging their headers and resized from the header edges. The account order is saved in the accounting file.

The first row is an opening-balance row. Enter each account's initial balance directly in that row; values are saved in the active accounting file. The app inserts a generated balance row after the last transaction of each month, showing each account's balance after that month.
