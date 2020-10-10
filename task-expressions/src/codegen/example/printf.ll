;
; Constant string for the printf format
; "%d\n" in C++
;
@f = constant [4 x i8] c"%d\0A\00", align 1

;
; Declaration of printf
;
declare i32 @printf(i8*, ...)

;
; Function foo
; in C++: int32_t foo(int32_t x, int32_t y) { int32_t tmp = x * y; printf("%d\n", tmp); return tmp; }
;
define i32 @foo(i32 %x, i32 %y) {
entry:
    %tmp = mul i32 %x, %y
    call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @f, i32 0, i32 0), i32 %tmp)
    ret i32 %tmp
}

;
; Function main
;
define i32 @main() {
entry:
    ; Allocate variables
    %x_r = alloca i32, align 4
    %y_r = alloca i32, align 4

    ; Store values
    store i32 2, i32* %x_r, align 4
    store i32 21, i32* %y_r, align 4

    ; Load the variables
    %x = load i32, i32* %x_r, align 4
    %y = load i32, i32* %y_r, align 4

    ; Call foo with our new values
    %r = call i32 @foo(i32 %x, i32 %y)
    ret i32 0
}

