function ceil(n,	i) {
	for (i = int(n); i < n; i++) { }
	return i;
}

function indent_level(s) {
	return ceil(length(s) / 8)
}

function repeat(s, n,	temp, i) {
	temp = ""
	for (i = 0; i < n; i++) {
		temp = sprintf("%s%s", s, temp)
	}
	return temp
}

function print_newline_array(start, arr, arrlen,	sep, end, i) {
	sep = sprintf("%s\t", start)
	end = " \\\n"
	for (i = 1; i < arrlen; i++) {
		if (i == arrlen - 1) {
			end = "\n"
		}
		printf "%s%s%s", sep, arr[i], end
		if (i == 1) {
			sep = repeat("\t", indent_level(start))
		}
	}
}

function print_token_array(start, tokens, tokenslen,	wrapcol, arr, arrlen, row, col, i) {
	wrapcol = 80 - length(start) - 8
	arrlen = 1
	row = ""
	col = 0
	for (i = 1; i < tokenslen; i++) {
		col = col + length(tokens[i]) + 1
		if (col >= wrapcol) {
			arr[arrlen++] = row
			row = ""
			col = 0
		}
		if (row == "") {
			row = tokens[i]
		} else {
			row = sprintf("%s %s", row, tokens[i])
		}
	}
	arr[arrlen++] = row
	print_newline_array(start, arr, arrlen)
}

# Case insensitive Bubble sort because it's super easy and we don't
# need anything better here ;-)
function sort_array(arr, arrlen,	i, j, temp) {
	for (i = 1; i < arrlen; i++) {
		for (j = i + 1; j < arrlen; j++) {
			if (arr[i] > arr[j]) {
				temp = arr[j]
				arr[j] = arr[i]
				arr[i] = temp
			}
		}
	}
}
