/*
 * eliza.c - Classic 1966 Rogerian Psychotherapist Chatbot for SIX (/bin/eliza)
 *
 * Based on Joseph Weizenbaum's original DOCTOR script (MIT, 1966).
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/string.h>
#include <linux/unistd.h>

#define MAX_INPUT 256
#define MAX_WORDS 64

struct keyword_rule {
	const char *key;
	int num_responses;
	const char *responses[5];
};

static const struct keyword_rule rules[] = {
	{
		"CAN YOU", 3,
		{
			"Don't you believe that I can %s?",
			"Perhaps you would like to be able to %s?",
			"You want me to be able to %s?"
		}
	},
	{
		"CAN I", 2,
		{
			"Whether or not you can %s depends on you more than on me.",
			"Do you want to be able to %s?"
		}
	},
	{
		"YOU ARE", 3,
		{
			"What makes you think I am %s?",
			"Does it please you to believe I am %s?",
			"Perhaps you would like to be %s?"
		}
	},
	{
		"YOURE", 3,
		{
			"What makes you think I am %s?",
			"Does it please you to believe I am %s?",
			"Perhaps you would like to be %s?"
		}
	},
	{
		"I DONT", 3,
		{
			"Don't you really %s?",
			"Why don't you %s?",
			"Do you wish to be able to %s?"
		}
	},
	{
		"I FEEL", 3,
		{
			"Tell me more about such feelings.",
			"Do you often feel %s?",
			"Do you enjoy feeling %s?"
		}
	},
	{
		"WHY DONT YOU", 3,
		{
			"Do you really believe I don't %s?",
			"Perhaps in good time I will %s.",
			"Do you want me to %s?"
		}
	},
	{
		"WHY CANT I", 2,
		{
			"Do you think you should be able to %s?",
			"Why can't you %s?"
		}
	},
	{
		"ARE YOU", 3,
		{
			"Why are you interested in whether I am %s or not?",
			"Would you prefer if I were not %s?",
			"Perhaps I am %s in your fantasies."
		}
	},
	{
		"I CANT", 3,
		{
			"How do you know you can't %s?",
			"Have you tried?",
			"Perhaps you could %s now."
		}
	},
	{
		"I AM", 3,
		{
			"Did you come to me because you are %s?",
			"How long have you been %s?",
			"Do you believe it is normal to be %s?"
		}
	},
	{
		"IM", 3,
		{
			"Did you come to me because you are %s?",
			"How long have you been %s?",
			"Do you believe it is normal to be %s?"
		}
	},
	{
		"BECAUSE", 3,
		{
			"Is that the real reason?",
			"What other reasons might there be?",
			"Does that reason explain anything else?"
		}
	},
	{
		"SORRY", 3,
		{
			"Please don't apologize.",
			"Apologies are not necessary.",
			"What feelings do you have when you apologize?"
		}
	},
	{
		"DREAM", 3,
		{
			"What does that dream suggest to you?",
			"Do you dream often?",
			"What persons appear in your dreams?"
		}
	},
	{
		"HELLO", 2,
		{
			"How do you do. Please state your problem.",
			"Hello. What is on your mind?"
		}
	},
	{
		"HI", 2,
		{
			"How do you do. Please state your problem.",
			"Hello. What is on your mind?"
		}
	},
	{
		"MAYBE", 3,
		{
			"You don't seem quite certain.",
			"Why the uncertain tone?",
			"Can't you be more positive?"
		}
	},
	{
		"NO", 3,
		{
			"Are you saying 'no' just to be negative?",
			"You are being a bit negative.",
			"Why not?"
		}
	},
	{
		"YOUR", 3,
		{
			"Why are you concerned over my %s?",
			"What about your own %s?",
			"Are you worried about someone else's %s?"
		}
	},
	{
		"ALWAYS", 3,
		{
			"Can you think of a specific example?",
			"When?",
			"Really, always?"
		}
	},
	{
		"THINK", 3,
		{
			"Do you really think so?",
			"But you are not sure you %s?",
			"Do you doubt you %s?"
		}
	},
	{
		"SAME", 3,
		{
			"In what way?",
			"What resemblance do you see?",
			"What does that similarity suggest to you?"
		}
	},
	{
		"ALIKE", 3,
		{
			"In what way?",
			"What resemblance do you see?",
			"What does that similarity suggest to you?"
		}
	},
	{
		"FRIEND", 3,
		{
			"Why do you bring up the topic of friends?",
			"Do your friends worry you?",
			"Do you rely on your friends?"
		}
	},
	{
		"COMPUTER", 3,
		{
			"Do computers worry you?",
			"Why do you mention computers?",
			"What do you think machines have to do with your problem?"
		}
	},
	{
		"MOTHER", 3,
		{
			"Tell me more about your mother.",
			"What was your relationship with your mother like?",
			"How do you feel about your mother?"
		}
	},
	{
		"FATHER", 3,
		{
			"Tell me more about your father.",
			"How does that relate to your father?",
			"What was your relationship with your father like?"
		}
	},
	{
		"YES", 3,
		{
			"You seem quite positive.",
			"You are sure?",
			"I understand."
		}
	}
};

#define NUM_RULES (sizeof(rules) / sizeof(rules[0]))

static const char *fallbacks[] = {
	"I'm not sure I understand you fully.",
	"Please go on.",
	"What does that suggest to you?",
	"Do you feel strongly about discussing such things?",
	"That is interesting. Please continue.",
	"Tell me more about that."
};
#define NUM_FALLBACKS (sizeof(fallbacks) / sizeof(fallbacks[0]))

struct reflex {
	const char *orig;
	const char *repl;
};

static const struct reflex reflections[] = {
	{ "am", "are" },
	{ "are", "am" },
	{ "i", "you" },
	{ "me", "you" },
	{ "my", "your" },
	{ "myself", "yourself" },
	{ "yourself", "myself" },
	{ "your", "my" },
	{ "yours", "mine" },
	{ "you", "I" },
	{ "im", "you are" },
	{ "youre", "I am" },
	{ "was", "were" },
	{ "were", "was" }
};
#define NUM_REFLEX (sizeof(reflections) / sizeof(reflections[0]))

static unsigned long rng_state = 1;

static int next_rand(void)
{
	rng_state = rng_state * 1103515245UL + 12345UL;
	return (int)((rng_state >> 16) & 0x7fff);
}

static char to_upper(char c)
{
	if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
	return c;
}

static char to_lower(char c)
{
	if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
	return c;
}

/* Reflect pronouns in the rest of sentence: "my dog" -> "your dog" */
static void reflect_text(const char *in, char *out, size_t out_len)
{
	char word[64];
	size_t wp = 0;
	out[0] = '\0';

	while (*in) {
		if ((*in >= 'a' && *in <= 'z') || (*in >= 'A' && *in <= 'Z') ||
		    (*in >= '0' && *in <= '9') || *in == '\'') {
			if (wp < sizeof(word) - 1)
				word[wp++] = to_lower(*in);
		} else {
			if (wp > 0) {
				int found = 0;
				size_t r;
				word[wp] = '\0';
				for (r = 0; r < NUM_REFLEX; r++) {
					if (strcmp(word, reflections[r].orig) == 0) {
						if (strlen(out) + strlen(reflections[r].repl) + 2 < out_len) {
							if (out[0] != '\0') strcat(out, " ");
							strcat(out, reflections[r].repl);
						}
						found = 1;
						break;
					}
				}
				if (!found) {
					if (strlen(out) + strlen(word) + 2 < out_len) {
						if (out[0] != '\0') strcat(out, " ");
						strcat(out, word);
					}
				}
				wp = 0;
			}
		}
		in++;
	}

	if (wp > 0) {
		int found = 0;
		size_t r;
		word[wp] = '\0';
		for (r = 0; r < NUM_REFLEX; r++) {
			if (strcmp(word, reflections[r].orig) == 0) {
				if (strlen(out) + strlen(reflections[r].repl) + 2 < out_len) {
					if (out[0] != '\0') strcat(out, " ");
					strcat(out, reflections[r].repl);
				}
				found = 1;
				break;
			}
		}
		if (!found) {
			if (strlen(out) + strlen(word) + 2 < out_len) {
				if (out[0] != '\0') strcat(out, " ");
				strcat(out, word);
			}
		}
	}
}

/* Check if line contains keyword as a whole word */
static const char *find_keyword(const char *line, const char *key)
{
	size_t klen = strlen(key);
	const char *p = line;

	while (*p) {
		size_t i;
		for (i = 0; i < klen; i++) {
			if (to_upper(p[i]) != key[i])
				break;
		}
		if (i == klen) {
			/* Check word boundary */
			int left_ok = (p == line || p[-1] == ' ' || p[-1] == '\t');
			int right_ok = (p[klen] == '\0' || p[klen] == ' ' || p[klen] == '\t' ||
			                p[klen] == '?' || p[klen] == '.' || p[klen] == '!' || p[klen] == ',');
			if (left_ok && right_ok)
				return p + klen;
		}
		p++;
	}
	return NULL;
}

static const char *my_strstr(const char *h, const char *n)
{
	size_t i;
	if (!*n) return h;
	while (*h) {
		for (i = 0; n[i] && h[i] == n[i]; i++);
		if (!n[i]) return h;
		h++;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	char input[MAX_INPUT];
	char reflected[MAX_INPUT];
	size_t last_fallback = 0;

	rng_state = (unsigned long)time(0) ^ (unsigned long)getpid();

	printf("ELIZA (1966 Rogerian Psychotherapist, MIT DOCTOR script)\n");
	printf("Type 'bye', 'quit', or 'exit' when done.\n\n");
	printf("ELIZA: How do you do. Please tell me your problem.\n");

	for (;;) {
		const char *match_pos = NULL;
		int rule_idx = -1;
		size_t i;

		printf("> ");
		fflush(stdout);

		if (!fgets(input, sizeof(input), stdin))
			break;

		/* Strip trailing newline and punctuation */
		size_t len = strlen(input);
		while (len > 0 && (input[len - 1] == '\n' || input[len - 1] == '\r' ||
		                   input[len - 1] == ' ' || input[len - 1] == '\t'))
			input[--len] = '\0';

		if (len == 0) {
			printf("ELIZA: Please, don't be shy. Tell me what is on your mind.\n");
			continue;
		}

		/* Check for exit */
		if (find_keyword(input, "BYE") || find_keyword(input, "QUIT") ||
		    find_keyword(input, "EXIT") || find_keyword(input, "GOODBYE")) {
			printf("ELIZA: Goodbye. It was very interesting speaking with you.\n");
			break;
		}

		/* Match rules */
		for (i = 0; i < NUM_RULES; i++) {
			match_pos = find_keyword(input, rules[i].key);
			if (match_pos) {
				rule_idx = (int)i;
				break;
			}
		}

		if (rule_idx >= 0) {
			const struct keyword_rule *r = &rules[rule_idx];
			int resp_idx = next_rand() % r->num_responses;
			const char *tmpl = r->responses[resp_idx];

			/* If template has %s, format with reflected remainder */
			if (my_strstr(tmpl, "%s")) {
				while (*match_pos == ' ' || *match_pos == '\t')
					match_pos++;
				reflect_text(match_pos, reflected, sizeof(reflected));
				if (reflected[0] != '\0') {
					printf("ELIZA: ");
					printf(tmpl, reflected);
					printf("\n");
				} else {
					/* Remainder empty: fallback */
					printf("ELIZA: %s\n", fallbacks[last_fallback++ % NUM_FALLBACKS]);
				}
			} else {
				printf("ELIZA: %s\n", tmpl);
			}
		} else {
			/* No rule matched: cycle through fallbacks */
			printf("ELIZA: %s\n", fallbacks[last_fallback++ % NUM_FALLBACKS]);
		}
	}

	return 0;
}
