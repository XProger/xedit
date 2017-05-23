#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <cstring>

#ifdef WIN32
	#include <windows.h>
	#include <windowsx.h>
#endif

#ifdef __linux__
	#include <sys/time.h>
	#include <sys/ipc.h>
	#include <sys/shm.h>

	#include <X11/Xlib.h>
	#include <X11/Xatom.h>
	#include <X11/Xutil.h>
	#include <X11/extensions/XShm.h>
	
	#define VK_LEFT		113
	#define VK_UP		111
	#define	VK_RIGHT	114
	#define	VK_DOWN		116
	#define	VK_BACK		22
#endif

void convertFont(const char *inName, const char *outName) {
	FILE *f = fopen(inName, "rb");
	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *data = new char[size];
	fread(data, 1, size, f);
	fclose(f);

	struct Header {
		char	size;
		char	info[11];
		short	width;
		short	height;
		char	bpp;
		char	imgInfo;
	} *tga = (Header*)data;
	
	if (tga->width != 128 || tga->height != 256 || tga->bpp != 32) {
		printf("! wrong %s format\n", inName);
		delete data;
		return;
	}

	struct RGBA {
		unsigned char r, g, b, a;
	} *pix = (RGBA*)&data[sizeof(Header) + tga->size];
	
	int count = 256 * 16;
	unsigned char *res = new unsigned char[count];
	for (int i = 0; i < 256; i++) {
		int ox = (i % 16) * 8;
		int oy = (i / 16) * 16;

		for (int y = 0; y < 16; y++) {
			unsigned char &r = res[i * 16 + y] = 0;

			for (int x = 0; x < 8; x++) {
				int ix = ox + x;
				int iy = oy + y;
				r |= (pix[ix + iy * 128].r > 0) << x;
			}
		}
	}

	f = fopen(outName, "wb");
	fwrite(res, sizeof(res[0]), count, f);
	fclose(f);

	delete data;
	delete res;

	printf("convert %s -> %s\n", inName, outName);
}

#ifdef WIN32
	typedef unsigned int Color;
#endif

#ifdef __linux__
	typedef unsigned int Color;
#endif

Color toColor(unsigned int x) {
	if (sizeof(Color) == 4)
		return x;
	if (sizeof(Color) == 2)
		return (x & 0xF80000) >> 8 | (x & 0xFC00) >> 5 | (x & 0xFF) >> 3;
}

#define COLOR_CLEAR	toColor(0xFF000000)

struct Point {
	int x, y;
	Point() {}
	Point(int x, int y) : x(x), y(y) {}
};

struct Rect {
	int l, t, r, b;
	Rect() {}
	Rect(int l, int t, int r, int b) : l(l), t(t), r(r), b(b) {}
};

struct BitFont {
	unsigned char *data;

	BitFont(const char *name) {
		FILE *f = fopen(name, "rb");
		int rows = 256 * 16;
		data = new unsigned char[rows];
		fread(data, sizeof(data[0]), rows, f);
		fclose(f);
	}

	~BitFont() {
		delete data;
	}

	void putChar(unsigned char c, Color fColor, Color bColor, Color *pixel, int stride) {
		unsigned char *d = &data[c * 16];

		for (int y = 0; y < 16; y++) {
			unsigned char v = d[y];

			for (int x = 0; x < 9; x++) {
				if (x == 8 && c >= 0xC0 && c <= 0xDF)
					v = d[y] >> 7;

				if (v & 1)
					pixel[x] = fColor;
				else
					if (bColor != COLOR_CLEAR)
						pixel[x] = bColor;
				v >>= 1;
			}

			pixel = &pixel[stride];
		}
	}
};

struct Canvas {
	int		width, height;
	int		stride;
	Color	*pixels;
	Rect	rect;
	
#ifdef __linux__
	XShmSegmentInfo	shminfo;
	GC		gc;
	Display	*display;
	XImage	*image;

	Canvas(Display *display) : width(0), height(0), pixels(NULL), display(display), image(NULL) {
		int i;
		if (!XQueryExtension(display, "MIT-SHM", &i, &i, &i))
			printf("SHM is not supported\n");
		gc = DefaultGC(display, DefaultScreen(display));
	}
#endif

#ifdef WIN32
	Canvas() : width(0), height(0), pixels(NULL) {}
#endif

	~Canvas() {
		resize(0, 0);
	}

	void resize(int width, int height) {
		if (this->width != width || this->height != height) {
			printf("resize %d %d\n", width, height);

			this->width = width;
			this->height = height;
		#ifdef WIN32
			pixels = (Color*)realloc(pixels, width * height * 4);
			stride = width;
		#endif
		
		#ifdef __linux__
			if (image) {
				XShmDetach(display, &shminfo);
				XDestroyImage(image);
				shmdt(shminfo.shmaddr);
			}
			
			if (!width || !height)
				return;
				
			int depth = DefaultDepth(display, DefaultScreen(display));
			printf("color depth:# %d\n", depth);

			image = XShmCreateImage(display, NULL, depth, ZPixmap, NULL, &shminfo, width, height);

			shminfo.shmid		= shmget(IPC_PRIVATE, image->bytes_per_line * image->height, IPC_CREAT|0777);
			shminfo.shmaddr		= image->data = (char*)shmat(shminfo.shmid, 0, 0);
			shminfo.readOnly	= false;

			XShmAttach(display, &shminfo);
			XSync(display, false);
			shmctl(shminfo.shmid, IPC_RMID, 0);

			pixels = (Color*)image->data;
			stride = image->bytes_per_line / sizeof(Color);
		#endif
		}
	}
	
#ifdef WIN32
	void present(HDC dc) {
		if (rect.l > rect.r || rect.t > rect.b)
			return;
	//	RECT r = { 0, 0, width, height };
	//	FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
		BITMAPINFO bmi = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
		SetDIBitsToDevice(dc, rect.l, rect.t, rect.r - rect.l, rect.b - rect.t, rect.l, rect.t, rect.t, rect.b, pixels, &bmi, 0);
	//	SetDIBitsToDevice(dc, 0, 0, width, height, 0, 0, 0, height, pixels, &bmi, 0);
	}
#endif

#ifdef __linux__
	void present(const Window &window) {
		if (rect.l > rect.r || rect.t > rect.b)
			return;
		XShmPutImage(display, window, gc, image, rect.l, rect.t, rect.l, rect.t, rect.r - rect.l, rect.b - rect.t, false);
		XFlush(display);
		XSync(display, false);
	}
#endif
	void scrollX(int delta) {

	}

	void scrollY(int offset) {
		int size = width * (height - abs(offset)) * 4;
		if (size <= 0) return;

		if (offset > 0)
			memmove(&pixels[offset * width], &pixels[0], size);
		else
			memmove(&pixels[0], &pixels[-offset * width], size);
	}

	void fill(int x, int y, int w, int h, Color color) {
		for (int j = y; j < y + h; j++)
			for (int i = x; i < x + w; i++)
				pixels[i + j * stride] = color;
	};
};

struct Editor {
private:
	BitFont	*font;
	char	*text;
	Point	cursor;
	Point	scroll;
	Point	offset;
	bool	valid;
public:
	enum ThemeColor {
		COLOR_CODE,
		COLOR_TEXT,
		COLOR_TYPE,
		COLOR_DEFINE,
		COLOR_NUMBER,
		COLOR_OPCODE,
		COLOR_COMMENT,
		COLOR_ARGUMENT,
		COLOR_BACK_LINE,
		COLOR_BACK_NORMAL,
		COLOR_BACK_SELECTION,
		COLOR_BACK_SEARCH,
		COLOR_CURSOR,
		COLOR_MAX
	};

	struct Theme {
		union {
			struct {
				Color byID[COLOR_MAX];
			};
			struct {
				Color code;
				Color text;
				Color type;
				Color define;
				Color number;
				Color opcode;
				Color comment;
				Color argument;
				Color back_line;
				Color back_normal;
				Color back_selection;
				Color back_search;
				Color cursor;
			};
		};
	} theme;

	struct Syntax {

		struct Lexeme {
			enum ID { 
				ID_CODE, ID_TEXT, ID_TYPE, ID_DEFINE, ID_NUMBER, ID_OPCODE, ID_COMMENT, ID_ARGUMENT, ID_MAX = 0xFFFFFFFF
			} id;
			int	offset;
			int	length;
		} *lexeme;
		int count;

		Syntax() : lexeme(NULL) {}
		
		~Syntax() { 
			if (lexeme) free(lexeme);
		}

		void lexemeBegin(int pos, Lexeme::ID id) {
			if (count && !lexeme[count - 1].length)
				return;

			lexeme = (Lexeme*)realloc(lexeme, (count + 1) * sizeof(Lexeme));
			lexeme[count].id		= id;
			lexeme[count].offset	= pos;
			lexeme[count].length	= 0;
			count++; 
		};

		void lexemeEnd(int pos) {
			if (count && !lexeme[count - 1].length)
				lexeme[count - 1].length = pos - lexeme[count - 1].offset;
		};

		bool checkOpcode(const char *str) {
			const char *opcodes[] = {	"void", "char", "bool", "short", "int", "long", "float", "double", "this", "typedef", "unsigned", "enum", "union",
										"sizeof", "return", "const", "static", "struct", "public", "private", "protected", "virtual", "new", "delete",
										"for", "while", "do", "true", "false", "if", "else", "continue", "break", "switch", "case", "default" };

			for (int i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); i++)
				if (!strcmp(str, opcodes[i]))
					return true;
			return false;
		}

		bool checkArgument(const char *str) {
			const char *args[] = { "#include", "#define", "#undef", "#if", "#ifdef", "#ifndef", "#else", "#endif" };
			for (int i = 0; i < sizeof(args) / sizeof(args[0]); i++)
				if (!strcmp(str, args[i]))
					return true;
			return false;
		}
		
		bool checkDefine(const char *str) {
			const char *defines[] = {	"NULL", "SEEK_END", "SEEK_CUR", "SEEK_SET", "COLOR_CLEAR", "VK_LEFT", "VK_RIGHT", "VK_UP", "VK_DOWN", "VK_BACK", "CALLBACK", "GetWindowLong", "SetWindowLong", "WIN32", "_DEBUG",
										"GWL_USERDATA", "GWL_WNDPROC", "LOWORD", "HIWORD", "GET_WHEEL_DELTA_WPARAM", "GET_X_LPARAM", "GET_Y_LPARAM", "WM_PAINT", "WM_SIZE", "WM_KEYDOWN", "WM_CHAR", "WM_MOUSEWHEEL", 
										"WM_LBUTTONDOWN", "WM_LBUTTONUP", "WM_RBUTTONDOWN", "WM_RBUTTONUP", "WM_DESTROY", "DefWindowProc", "CreateWindow", "WS_OVERLAPPEDWINDOW", "SW_SHOWDEFAULT", "GetMessage", "DispatchMessage" };
			for (int i = 0; i < sizeof(defines) / sizeof(defines[0]); i++)
				if (!strcmp(str, defines[i]))
					return true;
			return false;
		}

		bool checkType(const char *str) {
			const char *types[] = { "FILE", "BITMAPINFO", "BITMAPINFOHEADER", "MSG", "LONG", "Header", "RGBA", "Point", "Rect", "Color", "Font",  "Canvas", "Editor", "Theme", "Syntax", "Lexeme", "Window", "HWND", "HDC", "LRESULT", "UINT", "WPARAM", "LPARAM" };
			for (int i = 0; i < sizeof(types) / sizeof(types[0]); i++)
				if (!strcmp(str, types[i]))
					return true;
			return false;
		}

		void parse(const char *text) {
			count = 0;
			if (!text) return;

			char	tagText	= '\0';
			char	tagComm	= '\0';
			char	last	= '\0';
			int		length	= strlen(text);

			for (int i = 0; i < length; i++) {
				char c = text[i];

				if (c == '\\')
					i++;
				else
					if (c == tagText) {
						lexemeEnd(i + 1);
						tagText = '\0';
					} else
						if (( (c == '\r' || c == '\n') && (tagComm == '\0' || tagComm == '/')) || (tagComm == '*' && c == '/' && last == '*' && i++)) {
							lexemeEnd(i);
							tagComm = '\0';
						} else
							if (tagText == '\0' && tagComm == '\0')
								if (c == '/' && (text[i + 1] == '/' || text[i + 1] == '*')) {
									tagComm = text[i + 1];
									lexemeBegin(i, Lexeme::ID_COMMENT);
								} else
									if (c == '\'' || c == '"') {
										tagText = c;
										lexemeBegin(i, Lexeme::ID_TEXT);
									} else
										if (c == ' ' || c == '\t')
											lexemeEnd(i);
										else
											if (c >= '0' && c <= '9')
												lexemeBegin(i, Lexeme::ID_NUMBER);
											else
												if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '#')
													lexemeBegin(i, Lexeme::ID_CODE);
												else {
													lexemeEnd(i);
													lexemeBegin(i, Lexeme::ID_CODE);
													lexemeEnd(i + 1);
												}
				last = c;
			}
			lexemeEnd(length);

			printf("lexeme count: %d\n", count);
			for (int i = 0; i < count; i++) {
				Lexeme &lex = lexeme[i];
				char *str = new char[lex.length + 1];
				memcpy(str, &text[lex.offset], lex.length);
				str[lex.length] = '\0';

				if (checkOpcode(str))
					lex.id = Lexeme::ID_OPCODE;
				else
					if (checkDefine(str))
						lex.id = Lexeme::ID_DEFINE;
					else
						if (checkArgument(str))
							lex.id = Lexeme::ID_ARGUMENT;
						else
							if (checkType(str))
								lex.id = Lexeme::ID_TYPE;
			/*
				char *t;
				switch (lex.id) {
					case Lexeme::ID_OPCODE :
						t = "O";
						break;
					case Lexeme::ID_NUMBER :
						t = "N";
						break;
					case Lexeme::ID_TEXT :
						t = "T";
						break;
					case Lexeme::ID_COMMENT :
						t = "/";
						break;
					case Lexeme::ID_ARGUMENT :
						t = "A";
						break;
					default :
						t = "C";
				}

				printf("%d\t%s\t |%s|\n", i, t, str);
			*/
				delete str;
			};

		};

	} syntax;

	struct Cell {
		union {
			struct {
				int	id;
			};
			struct {
				char	c;
				char	fColor;
				char	bColor;
				char	reserved;
			};
		};
	} *cells;
	int	cols, rows;

	ThemeColor	fColor, bColor;
	int			curBuffer;

	Editor(const Theme &theme) : text(NULL), cursor(0, 0), scroll(0, 0), offset(0, 0), valid(false), theme(theme), cells(NULL), cols(0), rows(0), curBuffer(0) {
		font = new BitFont("font.dat");

		FILE *f = fopen("main.cpp", "rb");

		fseek(f, 0, SEEK_END);
		int size = ftell(f);
		fseek(f, 0, SEEK_SET);
		text = (char*)malloc(size + 1);
		fread(text, 1, size, f);
		text[size] = '\0';
		fclose(f);
		syntax.parse(text);
	}

	~Editor() { 
		delete font;
		if (text) free(text);
		if (cells) free(cells);
	}

	void invalidate(const Rect &rect) {
		valid = false;
	}

	void onKey(int key) {
		if (key == VK_LEFT)		cursor.x--;
		if (key == VK_RIGHT)	cursor.x++;
		if (key == VK_UP)		cursor.y--;
		if (key == VK_DOWN)		cursor.y++;
		if (key == VK_BACK)		if (text && strlen(text)) { text[strlen(text) - 1] = '\0'; syntax.parse(text); }

		valid = false;
	};

	void onChar(unsigned char c) {
		if (c < ' ' && c != '\r' && c != '\t')
			return;

		int len = text ? strlen(text) : 0;
		text = (char*)realloc(text, len + 2);
		text[len]		= c;
		text[len + 1]	= 0;

		syntax.parse(text);

		valid = false;
	};

	void onScroll(int x, int y) {
		offset.x += x;
		offset.y += y;
		valid = false;
	}

	void resize(int width, int height) {
		int c = (width + 8) / 9;
		int r = (height + 15) / 16;

		if (cols * rows != c * r) {
			cells = (Cell*)realloc(cells, 2 * c * r * sizeof(cells[0]));
			for (int i = 0; i < 2 * c * r; i++) {
				cells[i].c			= '\0';
				cells[i].bColor		= COLOR_BACK_NORMAL;
				cells[i].bColor		= COLOR_BACK_NORMAL;
				cells[i].reserved	= 1;
			}
			valid = false;
		}
		cols = c;
		rows = r;
	};

	void putChar(int x, int y, unsigned char c) {
		if (x < 0 || y < 0 || x >= cols || y >= rows)
			return;

		Cell &cell = cells[x + y * cols + (curBuffer ? cols * rows : 0)];

		if (cell.c == c && cell.fColor == fColor && cell.bColor == bColor)
			return;

		cell.c		= c;
		cell.fColor	= fColor;
		cell.bColor	= bColor;
	};

	void print(int ox, int &x, int &y, ThemeColor fColor, ThemeColor bColor, const char *text, int length) {
		if (!length) return;

		this->fColor = fColor;
		this->bColor = bColor;

		for (int i = 0; i < length; i++)
			switch (text[i]) {
				case '\n' :
				//	break;
				case '\r' :
					x = 0;
					y++;
					break;
				case '\t' :
					x = (x / 4 + 1) * 4;
					break;
				default :
					putChar(ox + x, y, text[i]);
					x++;
			}
	}

	void render(Canvas *canvas) {
		if (offset.x) {
		//	canvas->scrollX(offset.x * 9);
			scroll.x += offset.x;
			offset.x = 0;
		}

		if (offset.y) {
		//	canvas->scrollY(offset.y * 16);
			scroll.y += offset.y;
			offset.y = 0;
		}

		if (!valid) {
			curBuffer ^= 1;
			Cell *c = &cells[curBuffer ? cols * rows : 0];
			for (int i = 0; i < cols * rows; i++) {
				c->c		= '\0';
				c->fColor	= COLOR_BACK_NORMAL;
				c->bColor	= COLOR_BACK_NORMAL;
				c->reserved	= 0;
				c++;
			}

			Point pos = Point(scroll.x, scroll.y);

			valid = true;
			int ox = 5;

			if (text) {
				int length = strlen(text);
				int i = 0;
				int lexIndex = 0;
				Syntax::Lexeme *lex = syntax.count ? &syntax.lexeme[0] : NULL;

				while (i < length) {
					if (lex && i == lex->offset) {
						ThemeColor color = (ThemeColor)lex->id;
						print(ox, pos.x, pos.y, color, COLOR_BACK_NORMAL, &text[lex->offset], lex->length);

						i += lex->length;
						lex = ++lexIndex == syntax.count ? NULL : &syntax.lexeme[lexIndex];
					} else {
						print(ox, pos.x, pos.y, COLOR_CODE, COLOR_BACK_NORMAL, &text[i], 1);
						i++;
					}
				}
			}

			print(ox, pos.x, pos.y, COLOR_CURSOR, COLOR_BACK_NORMAL, "\xDD", 1);

			int lines = pos.y - scroll.y + 1;
			char num[4];
			pos = Point(0, scroll.y);
			for (int i = 0; i < lines; i++) {
				snprintf(num, sizeof(num), "%d", i);
			//	itoa(i + 1, num, 10);
				int len = strlen(num);
				pos.x = (3 - len);
				print(0, pos.x, pos.y, COLOR_OPCODE, COLOR_BACK_NORMAL, num, len);
				pos.y++;
			}

			canvas->rect = Rect(0, 0, canvas->width, canvas->height);

			//canvas->clear(theme.back_normal);
			for (int r = 0; r < rows; r++)
				for (int c = 0; c < cols; c++) {
					Cell &cell = cells[c + r * cols + (curBuffer ? cols * rows : 0)];
					Cell &last = cells[c + r * cols + (curBuffer ? 0 : cols * rows)];

					if (cell.id != last.id) {
						if (cell.c != '\0')
							font->putChar(cell.c, theme.byID[cell.fColor], theme.byID[cell.bColor], &canvas->pixels[c * 9 + r * 16 * canvas->stride], canvas->stride);
						else
							canvas->fill(c * 9, r * 16, 9, 16, theme.byID[cell.bColor]);

					}/* else {
						canvas->fill(c * 9, r * 16, 9, 1, theme.byID[COLOR_DEFINE]);
						canvas->fill(c * 9, r * 16 + 15, 9, 1, theme.byID[COLOR_DEFINE]);
						canvas->fill(c * 9, r * 16, 1, 16, theme.byID[COLOR_DEFINE]);
						canvas->fill(c * 9 + 8, r * 16, 1, 16, theme.byID[COLOR_DEFINE]);
					}*/
				}
		}
	}
};

static const Editor::Theme THEME_DARK = {
	toColor(0xDADADA), // code
	toColor(0xD69D85), // text
	toColor(0x4EC9B0), // type
	toColor(0xBD63C5), // define
	toColor(0xB5CEA8), // number
	toColor(0x569CD6), // opcode
	toColor(0x57A64A), // comment
	toColor(0x7F7F7F), // argument
	toColor(0x000000), // back_line
	toColor(0x1E1E1E), // back_normal
	toColor(0x264F78), // back_selection
	toColor(0x653306), // back_search
	toColor(0xDCDCDC), // cursor
};

struct Application {
	int		width, height;
	Canvas	*canvas;
	Editor	*editor;

#ifdef WIN32
	HWND	handle;
	HDC		dc;

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		Application *app = (Application*)GetWindowLong(hWnd, GWL_USERDATA);

		switch (msg) {
			case WM_PAINT :
				app->paint();
				ValidateRect(hWnd, NULL);
				break;
			case WM_SIZE :
				app->resize(LOWORD(lParam), HIWORD(lParam));
				break;
			case WM_KEYDOWN :
				app->editor->onKey(wParam);
				app->invalidate();
				break;
			case WM_CHAR :
				app->editor->onChar(wParam);
				app->invalidate();
				break;
			case WM_MOUSEWHEEL :
				app->editor->onScroll(0, GET_WHEEL_DELTA_WPARAM(wParam) / 120);
				app->invalidate();
				break;
			case WM_LBUTTONDOWN : 
			case WM_RBUTTONDOWN : {
					int x = GET_X_LPARAM(lParam);
					int y = GET_Y_LPARAM(lParam);
				}
				break;
			case WM_DESTROY :
				app->close();
				break;
			default :
				return DefWindowProc(hWnd, msg, wParam, lParam);
		}
		return 0;
	}
#endif

#ifdef __linux__
	Display	*display;
	Window	window;
	Atom	WM_DELETE_WINDOW;
#endif

	Application(int width, int height) : width(width), height(height) {
	#ifdef WIN32
		canvas = new Canvas();
		editor = new Editor(THEME_DARK);

		handle = CreateWindow("static", "xedit", WS_OVERLAPPEDWINDOW, 0, 0, width, height, NULL, NULL, NULL, NULL);
		dc = GetDC(handle);
		SetWindowLong(handle, GWL_USERDATA, (LONG)this);
		SetWindowLong(handle, GWL_WNDPROC, (LONG)&WndProc);
		ShowWindow(handle, SW_SHOWDEFAULT);
	#endif
	
	#ifdef __linux__
		display = XOpenDisplay(NULL);
		if (!display) {
			printf("can't connect to X server\n");
			return;
		}		
		Window root = DefaultRootWindow(display);
		int screen = DefaultScreen(display);
		
		XSetWindowAttributes attr;
		attr.event_mask = ExposureMask | PointerMotionMask | ButtonPressMask | KeyPressMask | StructureNotifyMask | FocusChangeMask;		

		window = XCreateWindow(display, root, 0, 0, width, height, 0, 0, InputOutput, NULL, CWEventMask, &attr);
					
		XMapWindow(display, window);
		XSync(display, false);
		
		XStoreName(display, window, "xedit");
		WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", false);
		XSetWMProtocols(display, window, &WM_DELETE_WINDOW, 1);
		
		canvas = new Canvas(display);
		editor = new Editor(THEME_DARK);
		resize(800, 600);
	#endif
	}

	~Application() {
		delete editor;
		delete canvas;
	#ifdef WIN32
		ReleaseDC(handle, dc);
		DestroyWindow(handle);
	#endif
	
	#ifdef __linux__
		XDestroyWindow(display, window);
		XCloseDisplay(display);
	#endif
	}

	void close() {
	#ifdef WIN32
		PostQuitMessage(0);
	#endif
	}

	void invalidate() {
	#ifdef WIN32
		InvalidateRect(handle, NULL, false);
	#endif
	
	#ifdef __linux__
		XClearArea(display, window, 0, 0, width, height, True);
	#endif	
	}

	void resize(int width, int height) {
		editor->resize(width, height);
		canvas->resize(editor->cols * 9, editor->rows * 16);
	}

	void loop() {
	#ifdef WIN32
		MSG msg;
		while (GetMessage(&msg, 0, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);			
		}
	#endif
	
	#ifdef __linux__
		XEvent e;
		bool quit = false;
		while (!quit) {
			XNextEvent(display, &e);
			switch (e.type) {
				case FocusIn:
					invalidate();
					paint();
				break;
				case ButtonPress :
					if (e.xbutton.button == 4)	editor->onScroll(0, +1);
					if (e.xbutton.button == 5)	editor->onScroll(0, -1);
					invalidate();
					break;				
				case MotionNotify :
				//	printf("mouse: %d %d\n", e.xmotion.x, e.xmotion.y);
				//	offset = e.xmotion.y;
				//	draw(display, window, DefaultGC(display, screen));
					break;
				case KeyPress: {
					//	printf("key: %d %d\n", e.xkey.state, e.xkey.keycode);
						char c;
						if (e.xkey.keycode != VK_BACK &&
							XLookupString(&e.xkey, &c, 1, NULL, NULL)) {
							editor->onChar(c);
						//	printf("char %d %d\n", len, (int));
						} else
							editor->onKey(e.xkey.keycode);
						invalidate();
					}
					break;
				case ConfigureNotify : {
						width	= e.xconfigure.width;
						height	= e.xconfigure.height;
						resize(width, height);
					}
					break;
				case Expose :
					paint();
					break;
				case ClientMessage :
					if ((Atom)e.xclient.data.l[0] == WM_DELETE_WINDOW)
						quit = true;
					break;					
			}
			
		}	
	#endif
	}

	void paint() {
		editor->render(canvas);
	#ifdef WIN32
		canvas->present(dc);
	#endif
	#ifdef __linux__
		canvas->present(window);
	#endif
	}
};

int main() {
//	convertFont("font.tga", "font.dat");
	Application *app = new Application(800, 600);
	app->loop();
	delete app;
	return 0;
};
