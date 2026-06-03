# Magic Counting

Magic Counting is a Qt Widgets double-accounting app. The main window edits account transactions in a table. Each transaction has a date, a source account, an amount, the other party, one or more target account splits, a memo, and computed posting columns for every configured account.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/magiccounting
```

The project targets C++17 and Qt 5 Widgets.

You can pass the accounting file as an optional command-line argument:

```sh
./build/magiccounting /path/to/books.macc
```

The UI uses the system locale by default. Finnish localization is included and is loaded automatically for Finnish system locales. Use `Language` in the menu bar to choose System default, English, or Finnish; the choice is stored in the application configuration file.

## Data File

The app stores editable text data in one JSON file with the `.macc` extension. The file contains accounts, parties, and transactions in one portable document. This is the primary file intended for file associations and double-click opening.

Use `File -> Open Accounting File...` to load another `.macc` file. Transaction edits, account changes, parties, and opening balances are written back to the active `.macc` file. Use `File -> Save As...` to write the current accounting data to a different `.macc` file and make that file active.

The last opened accounting file is stored in the application configuration file `config.json` under Qt's application config folder, and is reopened on the next startup when no command-line file is provided. If no previous file exists, the app asks for a `.macc` file name at startup.

Legacy folders containing `accounts.json`, `parties.json`, and `transactions.json` can still be opened by passing the folder path on the command line; the app then saves the data into `accounting.macc` inside that folder.

Click or double-click the `Target accounts` cell to edit split rows in a popup. The popup keeps `OK` disabled until the target split total equals the transaction `Amount`. The source account posting is shown as a negative amount, and negative postings are rendered in red.

The first row is an opening-balance row. Enter each account's initial balance directly in that row; values are saved in the `.macc` file. The app inserts a generated balance row after the last transaction of each month, showing each account's balance after that month.
